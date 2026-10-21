"""Independent installed readers for the two original qNaN Gaussian failures.

The owned 391-sample source uses authoritative Float32LE payload bits at (7,8).
No native output/PASS string, NumPy float cast, JSON null, injected exception,
finite overflow or positive-work barrier supplies this test's oracle. Actual
Gaussian validation may legitimately fail with zero completed kernel samples.
"""

import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import struct

import pytest


GROUP_ID = "P02a.shared_ui.20.busy-failure-cancel-atomicity"
VARIANTS = [GROUP_ID + ".nonfinite-source-preview-failure",
            GROUP_ID + ".nonfinite-source-apply-failure"]
CONTEXTS = ["imgui_cpp", "imgui_python"]
UI_CONTRACT_SHA = "2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255"
NUMERIC_CONTRACT_SHA = "3b3f040e01b93642d2b0d3d84db9574f7a9ee525b3a7f29dac7ad21ea03ea6c3"
REFERENCE_SHA = "8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762"
FINITE_SOURCE_SHA = "7b334761d36148554ea3ab8743b4df82453d1b639d3a2fc8a8d20d1174baffd6"
NAN_SOURCE_SHA = "4f73311838db5d9abb9bca8b95c65930b937abadffc43c5b7a76e3af152e3e4f"
NAN_INDEX = 8 * 23 + 7
VALIDATION_STAGE = "ImageJ Gaussian validation"
ERROR = "Gaussian source samples must be finite Float32 values"
CALIBRATION = {"spacing": [1, 1, 1], "origin": [0, 0, 0],
               "direction": [1, 0, 0, 0, 1, 0, 0, 0, 1], "unit": "pixels",
               "frame_interval": 1, "time_origin": 0, "time_unit": "s"}
SCOPE_FLAGS = ["full61_verified", "full122_verified", "family_complete",
               "release_accepted", "full_goal_complete"]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def integer(value, *, positive=False):
    assert type(value) is int and value >= (1 if positive else 0)
    return value


def no_constant(value):
    raise ValueError(f"Nonfinite JSON constant: {value}")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, f"Duplicate JSON key: {key}"
        result[key] = value
    return result


def decode(data):
    value = json.loads(data, parse_constant=no_constant, object_pairs_hook=unique_object)
    pending = [value]
    while pending:
        item = pending.pop()
        if type(item) is float:
            assert math.isfinite(item)
        elif type(item) is dict:
            pending.extend(item.values())
        elif type(item) is list:
            pending.extend(item)
    return value


def expected_source():
    original = bytearray()
    for y in range(17):
        for x in range(23):
            base = (37*x+61*y+13*x*y+19*(x % 3)+23*(y % 5)) % 256
            sample = (base-127)/8+(x-2*y)/16+((x*y) % 17)/256
            original.extend(struct.pack("<f", sample))
    assert len(original) == 391*4 and sha(original) == FINITE_SOURCE_SHA
    source = original.copy()
    source[NAN_INDEX*4:(NAN_INDEX+1)*4] = struct.pack("<I", 0x7FC00001)
    assert source[NAN_INDEX*4:(NAN_INDEX+1)*4] == b"\x01\x00\xc0\x7f"
    assert sha(source) == NAN_SOURCE_SHA
    return bytes(original), bytes(source)


def expected_rgb(source):
    result = bytearray()
    for index, (bits,) in enumerate(struct.iter_unpack("<I", source)):
        if index == NAN_INDEX:
            assert bits == 0x7FC00001
            byte = 0  # Explicit owned NaN LUT index0, not upstream parity.
        else:
            assert bits & 0x7F800000 != 0x7F800000
            value = struct.unpack("<f", struct.pack("<I", bits))[0]
            byte = math.floor(min(255, max(0, (value+32)*(255/96))) + 0.5)
        result.extend((byte, byte, byte))
    return bytes(result)


def configured_pin(path_name, hash_name, expected):
    configured, digest = os.environ.get(path_name), os.environ.get(hash_name)
    assert configured and digest, f"Required NaN failure evidence missing: {path_name}/{hash_name}"
    assert digest == expected
    data = Path(configured).resolve(strict=True).read_bytes()
    assert sha(data) == expected
    return data


@pytest.fixture(scope="module")
def nonfinite_evidence():
    assert os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0") in {"0", "1"}
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") != "1" and not os.environ.get(
            "IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON"):
        pytest.skip("Installed NaN failure observations are optional outside a required desktop gate")
    ui = decode(configured_pin("IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON",
                               "IMAGEJXX_GAUSSIAN_UI_CONTRACT_SHA256", UI_CONTRACT_SHA))
    numerical = decode(configured_pin("IMAGEJXX_GAUSSIAN_CONTRACT_JSON",
                                      "IMAGEJXX_GAUSSIAN_CONTRACT_SHA256", NUMERIC_CONTRACT_SHA))
    reference = configured_pin("IMAGEJXX_GAUSSIAN_REFERENCE_JSONL",
                               "IMAGEJXX_GAUSSIAN_REFERENCE_SHA256", REFERENCE_SHA)
    assert numerical["comparison"]["expected_cases"] == 332
    variants = [item for group in ui["cases"] if group["id"] == GROUP_ID
                for item in group["variants"] if item["id"] in VARIANTS]
    assert [item["id"] for item in variants] == VARIANTS
    assert sum(len(item["steps"]) for item in variants) == 10
    assert [item["assertions"] for item in variants] == [
        ["genuine_kernel_validation_failure", "job_error_observed",
         "no_transient_installed_on_failure", "unchanged_document", "no_gaussian_workflow_record"],
        ["genuine_kernel_validation_failure", "job_error_observed",
         "unchanged_document", "no_gaussian_workflow_record"]]
    for item in variants:
        assert item["required_contexts"] == CONTEXTS
        assert item["setup"] == {"owned_input": "nonfinite_f32_17x23", "fresh_host_session": True,
                                 "mark_saved_before": True, "recording": False}
    owned = ui["owned_inputs"]["nonfinite_f32_17x23"]
    assert owned["dtype"] == "F32" and owned["shape"] == [17, 23]
    assert owned["reference_observation"] is False
    assert "(x7,y8)" in owned["formula"] and "0x7fc00001" in owned["formula"]
    finite, source = expected_source()
    ids, finite_reference = set(), None
    for line in reference.split(b"\n"):
        if not line.strip():
            continue
        record = decode(line)
        assert record["id"] not in ids
        ids.add(record["id"])
        if record["id"] == "f32-full-s0":
            assert record["dtype"] == "F32" and record["shape"] == [17, 23]
            assert record["input_formula"] == "owned-asymmetric-analytic-v1"
            finite_reference = bytes.fromhex(record["input_raw_hex"])
            assert finite_reference == finite and record["input_sha256"] == FINITE_SOURCE_SHA
    assert len(ids) == 332 and finite_reference is not None
    return {"variants": dict(zip(VARIANTS, variants)), "owned_input": owned,
            "source": source, "rgb": expected_rgb(source)}


def member_path(directory, name):
    assert type(name) is str and name and not Path(name).is_absolute()
    assert ".." not in Path(name).parts
    root = directory.resolve(strict=True)
    path = (root / name).resolve(strict=True)
    assert path.is_relative_to(root) and path.is_file()
    return path


def raw(member, directory, *, dtype, shape):
    path = member_path(directory, member["member_id"])
    declared = Path(member["path"])
    if not declared.is_absolute():
        declared = directory / declared
    assert declared.resolve(strict=True) == path
    stride, encoding = {"float32": (4, "IEEE754 Float32LE"), "uint8": (1, "UInt8")}[dtype]
    count = math.prod(shape)
    assert member["dtype"] == dtype and member["shape"] == shape and member["encoding"] == encoding
    assert integer(member["sample_count"]) == count and integer(member["byte_count"]) == count*stride
    data = path.read_bytes()
    assert len(data) == count*stride and sha(data) == member["sha256"]
    return data


def usage(value, *, empty=False):
    assert set(value) == {"running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"}
    for item in value.values():
        integer(item)
    assert value["reserved_bytes"] == value["running_bytes"] + value["retained_bytes"]
    if empty:
        assert not any(value.values())


def jobs(value, source_id):
    assert type(value) is list
    result = {}
    for job in value:
        job_id = integer(job["id"], positive=True)
        assert job_id not in result and job["document_id"] == source_id
        assert job["state"] in {"QUEUED", "RUNNING", "READY", "PUBLISHING", "COMPLETED", "FAILED", "CANCELED"}
        assert type(job["label"]) is type(job["error"]) is type(job["stage"]) is str
        assert type(job["cancel_requested"]) is bool
        for field in ("resource_bytes", "completed", "total", "checkpoints", "stage_completed", "stage_total"):
            integer(job[field])
        result[job_id] = job
    return result


def native_state(value, directory, expected):
    assert set(value["documents"]) == {"source"}
    source = value["documents"]["source"]
    identifier = integer(source["document_id"], positive=True)
    integer(source["data_identity"])
    assert source["shape_tcz_yxs"] == [1, 1, 1, 17, 23, 1]
    assert source["dtype"] == "float32" and source["sample_policy"] == "preserve_quiet_nan_float32"
    assert source["calibration"] == CALIBRATION
    assert type(source["dirty"]) is bool
    integer(source["undo_steps"])
    integer(source["redo_steps"])
    assert set(source["revisions"]) == {"pixels", "metadata", "selection", "display"}
    for item in source["revisions"].values():
        integer(item)
    assert source["history"]["undo_steps"] == source["undo_steps"]
    assert source["history"]["redo_steps"] == source["redo_steps"]
    integer(source["history"]["step_limit"])
    integer(source["history"]["pixel_budget"])
    assert source["history"]["pixels"]["exact"] is True
    for key, item in source["history"]["pixels"].items():
        if key != "exact":
            integer(item)
    assert [plane["position"] for plane in source["planes"]] == [[0, 0, 0]]
    plane = source["planes"][0]
    observed = raw(plane["raw"], directory, dtype="float32", shape=[17, 23])
    assert observed == expected and sha(observed) == NAN_SOURCE_SHA
    assert struct.unpack_from("<I", observed, NAN_INDEX*4)[0] == 0x7FC00001
    for field in ("image_address", "buffer_address"):
        assert type(plane[field]) is str and int(plane[field], 16) > 0
    info = source["document_info"]
    assert info["id"] == identifier and info["dtype"] == "float32"
    assert info["dirty"] is source["dirty"] and info["revisions"] == source["revisions"]
    assert info["undo_steps"] == source["undo_steps"] and info["redo_steps"] == source["redo_steps"]
    assert info["axis_sizes"] == {"T": 1, "C": 1, "Z": 1, "Y": 17, "X": 23, "S": 1}
    assert info["view"]["crosshair"] == source["display"]["crosshair"]
    assert value["active_document_id"] == identifier
    assert value["application_document_ids"] == value["desktop_document_ids"] == [identifier]
    assert value["recording"] is False and value["workflow"] == value["results"] == []
    return source


def unchanged(value, before, directory, expected):
    source = native_state(value, directory, expected)
    assert set(value) == set(before)
    for field in before:
        if field != "documents":
            assert value[field] == before[field], f"Failed job changed native {field}"
    original = before["documents"]["source"]
    assert set(source) == set(original)
    for field in original:
        if field != "planes":
            assert source[field] == original[field], f"Failed job changed source {field}"
    for current, prior in zip(source["planes"], original["planes"]):
        assert {key: item for key, item in current.items() if key != "raw"} == {
            key: item for key, item in prior.items() if key != "raw"}


def cpu_readback(rendered, directory, evidence, source_id):
    ordinary = rendered["ordinary_pixels"]
    assert ordinary["pixels_source"] == ordinary["contrast_source"] == "live"
    assert ordinary["source_document_id"] == source_id and ordinary["preview_generation"] == 0
    readback = ordinary["readback"]
    assert readback["available"] is True
    source, rgb = readback["source"], readback["rendered_rgb"]
    for image, dtype, shape, expected in ((source, "float32", [17, 23, 1], evidence["source"]),
                                          (rgb, "uint8", [17, 23, 3], evidence["rgb"])):
        assert image["dtype"] == dtype and image["shape"] == shape
        assert "pixels" not in image and "bytes" not in image
        assert "no GPU readback" in image["observation_scope"]
        assert raw(image["raw"], directory, dtype=dtype, shape=shape) == expected


def host(value):
    for field in ("admission_epoch", "current_epoch", "drained_epoch"):
        integer(value[field])
    for field in ("mirrored_ids", "actual_lane_ids"):
        assert type(value[field]) is list and len(value[field]) == 2
        for item in value[field]:
            integer(item)


def probe(value, source_id):
    integer(value["bound_job_id"])
    assert type(value["resource_guard_observed"]) is bool
    assert type(value["resource_guard_expired_after_finalize"]) is bool
    assert value["working_owner_observed"] is False and value["working_owner"] is None
    assert type(value["working_owner_scope"]) is str and value["working_owner_scope"]
    events = value["events"]
    assert type(events) is list and [item["sequence"] for item in events] == list(range(1, len(events)+1))
    allowed = {"worker_entered", "stage_progress", "admission_before_submit",
               "admission_after_submit", "before_native_publish", "submit_returned_job_association"}
    for event in events:
        assert event["phase"] in allowed and event["role"] in {"host", "native_worker"}
        assert type(event["thread_id"]) is str and event["thread_id"]
        if event["phase"] in {"worker_entered", "stage_progress"}:
            assert event["role"] == "native_worker" and event["operation"] == "gaussian_blur_imagej"
            assert event["source_id"] == source_id
            completed, total = integer(event["completed"]), integer(event["total"])
            assert completed <= total and event["resource_guard_live_at_observation"] is True
            usage(event["usage"])
            if event["phase"] == "worker_entered":
                assert event["stage"] == "" and (completed, total) == (0, 0)
            elif event["stage"] == "Numeric arguments":
                # Argument validation is genuinely forwarded; its two values
                # are not completed Gaussian pixels or a positive-work proof.
                assert total == 2 and completed in {0, 2}
            else:
                assert event["stage"] == VALIDATION_STAGE
                assert completed == 0 and total == 391
        elif event["phase"] == "submit_returned_job_association":
            assert event["role"] == "host"
            integer(event["job_id"], positive=True)
            assert event["association_timing"] == "after actual submission returned"
            assert event["origin"] in {"actual Window Gaussian Preview admission_after_submit",
                                       "actual Window Gaussian Apply admission_after_submit"}
        else:
            assert event["role"] == "host"
            host(event)
    return events


def control(rendered, selector):
    key = {"gaussian_panel.controls.preview": "preview",
           "gaussian_panel.controls.ok": "ok", "gaussian_panel.controls.cancel": "cancel",
           "gaussian_panel.controls.sigma": "sigma"}[selector]
    value = dict(rendered["gaussian_panel"]["control_states"][key])
    value["rect"] = rendered["gaussian_panel"]["control_rects"][key]
    value["point"] = rendered["gaussian_panel"]["controls"][key]
    return value


def click_events(observation):
    return [[{"type": "position", "space": "screen", "point": observation["position"]}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}], []]


def frames_and_steps(row, directory, adopted, source_id):
    frames = row["frames"]
    assert frames and len(frames) <= 1200
    assert [frame["ordinal"] for frame in frames] == list(range(1, len(frames)+1))
    sessions, previous_probe = [], []
    for frame in frames:
        assert decode(member_path(directory, f"frame-{frame['ordinal']}.json").read_bytes()) == frame
        assert type(frame["busy"]) is bool
        usage(frame["usage"])
        assert len(jobs(frame["actual_native_jobs"], source_id)) <= 1
        host(frame["host"])
        events = probe(frame["native_probe"], source_id)
        assert events[:len(previous_probe)] == previous_probe
        previous_probe = events
        rendered = frame["rendered"]
        session = rendered["session"]
        assert session["open"] is True
        integer(session["session_id"], positive=True)
        integer(session["context_id"], positive=True)
        assert session["current_context_id"] == session["context_id"]
        sessions.append(session)
        inputs = rendered["input_frame"]
        assert inputs["raw_inputs"] == frame["requested_events"]
        for event in frame["requested_events"]:
            assert event["type"] in {"position", "button", "key", "keys", "text"}
            if event["type"] == "position":
                assert set(event) == {"type", "space", "point"} and event["space"] == "screen"
                assert inputs["mouse_position"] == [math.floor(item) for item in event["point"]]
            elif event["type"] == "button":
                assert event["name"] == "left" and type(event["down"]) is bool
                assert inputs["mouse_down"][0] is event["down"]
            elif event["type"] in {"key", "keys"}:
                atoms = event["keys"] if event["type"] == "keys" else [{"name": event["name"], "down": event["down"]}]
                for atom in atoms:
                    assert set(atom) == {"name", "down"} and type(atom["down"]) is bool
                    assert (atom["name"] in inputs["keys_down"]) is atom["down"]
            else:
                assert type(event["value"]) is str
        gaussian = rendered["gaussian_jobs"]
        assert integer(gaussian["apply_callback_count"]) == 0
        assert gaussian["installed_preview_storage"] is None
    assert len({life["session_id"] for life in sessions}) == len({life["context_id"] for life in sessions}) == 1
    for before, after in zip(sessions, sessions[1:]):
        assert after["imgui_frame"] == before["imgui_frame"]+1 and after["imgui_time"] > before["imgui_time"]
    traces = row["steps"]
    assert [trace["ordinal"] for trace in traces] == list(range(1, len(adopted["steps"])+1))
    assert [trace["adopted_step"] for trace in traces] == adopted["steps"]
    previous_last = 0
    observations = {}
    for observation in row["control_observations"]:
        ordinal = integer(observation["step_ordinal"], positive=True)
        observations.setdefault(ordinal, []).append(observation)
        before = integer(observation["before_frame_ordinal"], positive=True)
        actual = control(frames[before-1]["rendered"], observation["selector"])
        assert observation["control"] == actual and observation["attempted_disabled"] is False
        integer(actual["id"], positive=True)
        assert actual["enabled"] is True
        assert type(actual["active"]) is type(actual["focused"]) is bool
        x, y = observation["position"]
        assert type(x) in {int, float} and type(y) in {int, float}
        assert x == math.floor(x) and y == math.floor(y)
        rectangle = actual["rect"]
        assert len(rectangle) == 4 and all(type(item) in {int, float} and math.isfinite(item) for item in rectangle)
        assert rectangle[0] <= x < rectangle[2] and rectangle[1] <= y < rectangle[3]
        delivered = control(frames[before]["rendered"], observation["selector"])
        assert delivered["id"] == actual["id"] and delivered["enabled"] is True
        assert delivered["rect"][0] <= x < delivered["rect"][2] and delivered["rect"][1] <= y < delivered["rect"][3]
    for trace in traces:
        first, last = integer(trace["first_frame_ordinal"]), integer(trace["last_frame_ordinal"])
        assert previous_last <= first <= last <= len(frames)
        previous_last = last
        assert trace["before_frame_ordinal"] == first and trace["completed"] is True
        for frame in frames[first:last]:
            assert frame["step_ordinal"] == trace["ordinal"]
        step = trace["adopted_step"]
        actual = [frame["requested_events"] for frame in frames[first:last]]
        observed = observations.get(trace["ordinal"], [])
        if step["op"] == "click":
            assert len(observed) == 1 and observed[0]["selector"] == step["selector"]
            assert observed[0]["before_frame_ordinal"] == first and actual == click_events(observed[0])
        elif step["op"] == "replace_sigma":
            assert len(observed) == 1 and observed[0]["selector"] == "gaussian_panel.controls.sigma"
            assert observed[0]["before_frame_ordinal"] == first
            select_all = [[{"type": "keys", "keys": [{"name": "Ctrl", "down": down},
                                                      {"name": "A", "down": down}]}] for down in (True, False)]
            assert actual == click_events(observed[0]) + select_all + [[{"type": "text", "value": "2"}], []]
            focused = frames[first+3]["rendered"]["input_frame"]
            assert focused["focus"]["active_id"] == focused["input_text"]["id"] == observed[0]["control"]["id"]
            assert frames[last-1]["rendered"]["gaussian_panel"]["sigma_text"] == step["text"] == "2"
        elif step["op"] == "open_command":
            assert observed == [] and step["route"] == "labelled_action_preparation" and actual == [[]]
            panel = frames[last-1]["rendered"]["gaussian_panel"]
            assert panel["open"] is panel["visible"] is True and panel["source_id"] == source_id
            assert panel["sigma"] == 2 and panel["sigma_text"] == "2" and panel["preview"] is False
        elif step["op"] == "observe":
            assert observed == [] and first == last and actual == [] and step["label"] == "failed"
            assert trace["checkpoint_name"] == "failed"
        else:
            assert step["op"] == "await_native" and observed == [] and all(events == [] for events in actual)
            assert step["watchdog_seconds"] == 30 and step["max_pump_frames"] == 1200
        if step["op"] != "observe":
            assert trace["checkpoint_name"] is None
    return frames


def checkpoints(row, directory, frames, evidence, source_id, preview):
    names = ["baseline", "failed"] + (["dialog_canceled"] if preview else []) + ["cleanup"]
    assert [item["name"] for item in row["checkpoints"]] == names
    result, prior = {}, 0
    for item in row["checkpoints"]:
        assert decode(member_path(directory, "checkpoint-"+item["name"]+".json").read_bytes()) == item
        ordinal = integer(item["frame_ordinal"], positive=True)
        assert prior <= ordinal <= len(frames)
        prior = ordinal
        assert item["rendered"] == frames[ordinal-1]["rendered"]
        assert type(item["busy"]) is bool
        usage(item["usage"])
        jobs(item["actual_native_jobs"], source_id)
        host(item["host"])
        events = probe(item["native_probe"], source_id)
        prefix = frames[ordinal-1]["native_probe"]["events"]
        assert events[:len(prefix)] == prefix
        unchanged(item["native_state"], row["before"], directory, evidence["source"])
        cpu_readback(item["rendered"], directory, evidence, source_id)
        result[item["name"]] = item
    assert result["baseline"]["native_state"] == row["before"]
    observed = next(trace for trace in row["steps"] if trace["adopted_step"]["op"] == "observe")
    assert result["failed"]["frame_ordinal"] == observed["first_frame_ordinal"] == observed["last_frame_ordinal"]
    assert result["failed"]["step_ordinal"] == observed["ordinal"]
    return result


def captured_command(record, source):
    assert set(record) == {"schema_version", "operation", "options", "calibration", "effective_sigma_xy", "target"}
    assert type(record["schema_version"]) is int and record["schema_version"] == 1
    assert record["operation"] == "gaussian_blur_imagej"
    assert record["options"] == {"sigma": 2, "scaled_units": False}
    assert type(record["options"]["sigma"]) in {int, float} and record["options"]["scaled_units"] is False
    assert record["effective_sigma_xy"] == [2, 2] and record["calibration"] == CALIBRATION
    assert record["target"] == {
        "schema_version": 1, "document_id": source["document_id"],
        "source_revisions": [source["revisions"][key] for key in ("pixels", "metadata", "selection")],
        "source_shape": source["shape_tcz_yxs"], "scope": "ranges", "ranges": [[0, 1, 1]]*3,
        "positions": [], "roi_policy": "active_selection", "execution_mode": "planes", "destination": "in_place"}


def actual_failure(row, observed, source, preview):
    identifier = source["document_id"]
    terminal = row["terminal_job"]
    actual = jobs([terminal], identifier)
    job_id = next(iter(actual))
    assert terminal["state"] == "FAILED" and terminal["error"] == ERROR
    assert terminal["cancel_requested"] is False
    assert terminal["stage"] == VALIDATION_STAGE
    assert terminal["stage_completed"] == 0 and terminal["stage_total"] == 391
    integer(terminal["resource_bytes"], positive=True)
    assert [item["name"] for item in row["job_observations"]] == [
        "actual_gaussian_submitted", "actual_native_finalized_failed"]
    for item in row["job_observations"]:
        assert set(jobs([item["job"]], identifier)) == {job_id}
    assert row["job_observations"][-1]["job"] == terminal
    failed = observed["failed"]
    assert set(jobs(failed["actual_native_jobs"], identifier)) == {job_id}
    assert jobs(failed["actual_native_jobs"], identifier)[job_id] == terminal
    assert failed["busy"] is False
    usage(failed["usage"], empty=True)
    gaussian = failed["rendered"]["gaussian_jobs"]
    lane = gaussian["preview" if preview else "apply"]
    assert lane["job"]["id"] == job_id and lane["job"]["state"] == "failed"
    assert lane["error"] == ERROR and lane["exit"] == "job_rejected" and lane["owns_job"] is False
    assert lane["source_id"] == identifier and lane["published_document_id"] is None
    captured_command(lane["command_record"], source)
    assert lane["target"] == lane["command_record"]["target"]
    assert gaussian["installed_preview_storage"] is None
    assert gaussian["apply_callback_count"] == 0
    for field in ("running_reserved_bytes", "retained_reserved_bytes", "reserved_bytes",
                  "retained_preview_declared_bytes", "retained_apply_declared_bytes"):
        assert integer(gaussian["resource_usage"][field]) == 0
    if preview:
        assert failed["rendered"]["gaussian_panel"]["preview_error"] == ERROR
        canceled = observed["dialog_canceled"]["rendered"]["gaussian_panel"]
        assert canceled["open"] is False and canceled["exit_reason"] == "cancel"
    else:
        assert failed["rendered"]["window_error"] == ERROR
        assert failed["rendered"]["error_modal"]["open"] is True
    assert row["publication_observation"] == {
        "before_native_publish_probe_count": 0, "actual_apply_callback_count": 0,
        "published_document_id": None, "live_destination_document_id": identifier,
        "working_owner_observed": False, "working_owner": None}
    prepared = row["prepared_result_observation"]
    assert prepared["available"] is False
    assert type(prepared["error"]) is str and prepared["error"]
    assert prepared["observation_timing"] == "after actual Failed job finalized"
    native = row["native_probe"]
    assert native["bound_job_id"] == job_id and native["resource_guard_observed"] is True
    assert native["resource_guard_expired_after_finalize"] is True
    events = probe(native, identifier)
    before = [event for event in events if event["phase"] == "admission_before_submit"]
    after = [event for event in events if event["phase"] == "admission_after_submit"]
    association = [event for event in events if event["phase"] == "submit_returned_job_association"]
    entered = [event for event in events if event["phase"] == "worker_entered"]
    validation = [event for event in events if event["phase"] == "stage_progress" and event["stage"] == VALIDATION_STAGE]
    assert len(before) == len(after) == len(association) == len(entered) == 1 and validation
    assert before[0]["sequence"] < after[0]["sequence"] < association[0]["sequence"]
    assert entered[0]["sequence"] < validation[0]["sequence"]
    assert association[0]["job_id"] == job_id
    assert association[0]["origin"] == "actual Window Gaussian " + ("Preview" if preview else "Apply") + " admission_after_submit"
    assert integer(entered[0]["usage"]["running_bytes"], positive=True) > 0
    assert entered[0]["usage"]["retained_bytes"] == 0
    for event in events:
        assert event["phase"] != "before_native_publish"
        if event["role"] == "host":
            assert event["thread_id"] == row["creator_thread"]
        else:
            assert event["thread_id"] != row["creator_thread"]
            known = integer(event["job_id_known_at_observation"])
            assert known == (0 if event["sequence"] < association[0]["sequence"] else job_id)
    return job_id


def api_preparation(row, source_id):
    inputs = row["api_inputs"]
    assert [item["purpose"] for item in inputs] == [
        "explicit owned quiet-NaN fixture/default preparation only",
        "adopted labelled_action_preparation opening only",
        "bounded final/failure native-job cleanup only",
        "bounded owned-fixture teardown after atomicity assertions"]
    for item in inputs:
        integer(item["step_ordinal"])
        integer(item["frame_ordinal"])
    prep, opening, cleanup, teardown = inputs
    assert prep["step_ordinal"] == prep["frame_ordinal"] == 0 and prep["document_id"] == source_id
    assert prep["recording"] is False and prep["tested_changes_use_api_setters"] is False
    assert prep["methods"] == ["float32_source_sample(bits)", "canonical_float32_source",
                                "Dataset.from_stack(sample_policy)", "addNativeDocument",
                                "setNativeDisplay", "mark_saved"]
    assert opening["step_ordinal"] == 1 and opening["action"] == "show_operation"
    assert opening["parameters"] == {"operation": "gaussian_blur_imagej"}
    assert opening["opening_is_gesture_proof"] is False
    assert cleanup["methods"] == ["Window.cancel", "actual job wait_for5000ms", "Window.poll", "drop native job copy"]
    assert teardown["document_id"] == source_id
    assert teardown["methods"] == ["closeDocument(discard=true)", "drop original snapshot/document aliases"]


def assert_row(row, variant, context, directory, evidence):
    assert decode(member_path(directory, "case.json").read_bytes()) == row
    assert type(row["schema_version"]) is int and row["schema_version"] == 1
    assert row["group_id"] == GROUP_ID and row["case_id"] == variant and row["context"] == context
    assert row["status"] == "passed" and row["acceptance_claim"] is False
    assert row["contract_sha256"] == UI_CONTRACT_SHA and row["adopted_variant"] == evidence["variants"][variant]
    assert all(row[field] is False for field in SCOPE_FLAGS)
    assert row["ImageJ_nonfinite_propagation_verified"] is False
    assert type(row["creator_thread"]) is str and row["creator_thread"]
    assert "no GPU readback" in row["observation_scope"]
    expected = evidence["source"]
    assert raw(row["fixture_input_raw"], directory, dtype="float32", shape=[17, 23]) == expected
    assert row["owned_input"] == {
        "id": "nonfinite_f32_17x23", "reference_observation": False, "dtype": "F32", "shape": [17, 23],
        "sample_count": 391, "special_index": NAN_INDEX, "special_bits": "0x7fc00001",
        "expected_source_sha256": NAN_SOURCE_SHA}
    assert "replace(x7,y8)withFloat32bits0x7fc00001" in row["fixture_formula"]
    source = native_state(row["before"], directory, expected)
    source_id = source["document_id"]
    assert source["dirty"] is False and source["undo_steps"] == source["redo_steps"] == 0
    assert source["selection"] is source["previous_selection"] is None and source["spectrum_present"] is False
    assert source["metadata"] == {
        "title": "Owned group20 quiet-NaN source", "properties": {"nonfinite_property": "preserve"},
        "extensions": {"nonfinite_extension": "unknown-kept"}, "record_layout": {}}
    assert len(source["rois"]) == 1
    roi = source["rois"][0]
    assert roi["name"] == "retained-nonfinite-roi" and roi["t"] is roi["c"] is None and roi["z"] == 0
    assert roi["attributes"] == {"nonfinite_unknown": "roi-kept"}
    assert roi["selection"] == {"kind": "rectangle", "points": [[3, 2], [8, 7]], "width": 1,
                                 "nonfinite_shape_unknown": "shape-kept"}
    assert source["overlays"] == [{"kind": "point", "color": "#ffcc0000", "points": [[9, 13]],
                                   "nonfinite_unknown": "overlay-kept"}]
    assert source["display"]["crosshair"] == [7, 8, 0]
    assert source["display"]["properties"] == {"display_range": [-32, 64]}
    frames = frames_and_steps(row, directory, evidence["variants"][variant], source_id)
    preview = variant == VARIANTS[0]
    observed = checkpoints(row, directory, frames, evidence, source_id, preview)
    job_id = actual_failure(row, observed, source, preview)
    for frame in frames:
        actual_jobs = jobs(frame["actual_native_jobs"], source_id)
        assert set(actual_jobs) <= {job_id}
        assert not any(job["state"] in {"READY", "PUBLISHING", "COMPLETED", "CANCELED"} for job in actual_jobs.values())
    unchanged(row["after"], row["before"], directory, expected)
    for name in ("original_after_raw", "original_after_cleanup_raw"):
        assert raw(row[name], directory, dtype="float32", shape=[17, 23]) == expected
    usage(observed["cleanup"]["usage"], empty=True)
    api_preparation(row, source_id)
    final = row["final_cleanup"]
    usage(final["usage"], empty=True)
    assert set(jobs(final["actual_native_jobs"], source_id)) == {job_id}
    assert jobs(final["actual_native_jobs"], source_id)[job_id]["state"] == "FAILED"
    assert final["native_probe_expired"] is final["native_source_owner_expired"] is True
    assert final["resource_guard_expired_after_finalize"] is True
    assert integer(final["observer_retained_result_aliases"]) == 0
    assert final["application_document_ids"] == [] and integer(final["desktop_document_count"]) == 0
    assert [item["label"] for item in row["lifecycle"]] == ["opened", "before_close", "closed"]
    opened, before_close, closed = [item["observed"] for item in row["lifecycle"]]
    assert opened["open"] is before_close["open"] is True and closed["open"] is False
    assert opened["session_id"] == before_close["session_id"] == frames[0]["rendered"]["session"]["session_id"]
    assert opened["context_id"] == before_close["context_id"] == frames[0]["rendered"]["session"]["context_id"]
    assert closed["context_id"] == closed["current_context_id"] == 0 and final["lifecycle"] == closed


@pytest.fixture(scope="module")
def nonfinite_cpp_report():
    configured = os.environ.get("IMAGEJXX_NONFINITE_CPP_REPORT")
    if not configured:
        if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
            pytest.fail("Required installed standalone report missing: IMAGEJXX_NONFINITE_CPP_REPORT")
        pytest.skip("Standalone NaN failure report is optional outside a required desktop gate")
    path = Path(configured).resolve(strict=True)
    report = decode(path.read_bytes())
    assert type(report["schema_version"]) is int and report["schema_version"] == 1
    assert report["group_id"] == GROUP_ID and report["context"] == "imgui_cpp"
    assert report["status"] == "passed" and report["acceptance_claim"] is False
    assert report["contract_sha256"] == UI_CONTRACT_SHA
    assert all(report[field] is False for field in SCOPE_FLAGS)
    assert [row["case_id"] for row in report["cases"]] == VARIANTS
    return report, path.parent


def python_row(variant, directory):
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
        desktop = importlib.import_module("imagejxx._desktop")
    else:
        desktop = pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    assert Path(desktop.__file__).suffix in {".pyd", ".so"}
    editor = desktop.Editor(preferences_path=str(directory / "preferences.json"))
    return decode(editor._gaussian_nonfinite_case(variant=variant, context="imgui_python", directory=str(directory)))


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("variant", VARIANTS)
def test_installed_gaussian_nonfinite_ui(variant, context, request, tmp_path, nonfinite_evidence):
    if context == "imgui_cpp":
        report, root = request.getfixturevalue("nonfinite_cpp_report")
        row = next(item for item in report["cases"] if item["case_id"] == variant)
        directory = root / f"case{VARIANTS.index(variant)+1:02}"
    else:
        directory = tmp_path / "run"
        assert not directory.exists()
        row = python_row(variant, directory)
    assert_row(row, variant, context, directory, nonfinite_evidence)
