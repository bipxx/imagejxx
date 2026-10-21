"""Independent installed assertions for two finite adopted group20 variants.

The U8 source is independently constructed and reconciled with pinned capture002;
the large Float32 source is an owned analytic input. No native/producer output or
PASS text is an oracle. These four rows prove physical busy/cancel behavior and
failure atomicity; they provide no nonfinite-image, full61 or upstream UI credit.
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
VARIANTS = [GROUP_ID + ".busy-apply-blocked-cancel-usable",
            GROUP_ID + ".positive-work-actual-job-cancel"]
CONTEXTS = ["imgui_cpp", "imgui_python"]
UI_CONTRACT_SHA = "2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255"
NUMERIC_CONTRACT_SHA = "3b3f040e01b93642d2b0d3d84db9574f7a9ee525b3a7f29dac7ad21ea03ea6c3"
REFERENCE_SHA = "8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762"
U8_SOURCE_SHA = "f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4"
F32_SOURCE_SHA = "36d5aead1800a39eada5050052ba13e71190d45ef712af0a05d416522c233963"
COPY_STAGE = "ImageJ Gaussian source copy"
CALIBRATION = {"spacing": [1, 1, 1], "origin": [0, 0, 0],
               "direction": [1, 0, 0, 0, 1, 0, 0, 0, 1], "unit": "pixels",
               "frame_interval": 1, "time_origin": 0, "time_unit": "s"}
SCOPE_FLAGS = ["full61_verified", "full122_verified", "family_complete",
               "release_accepted", "full_goal_complete", "nonfinite_image_workload_qualified"]


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
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def decode(data):
    value = json.loads(data, parse_constant=no_constant, object_pairs_hook=unique_object)
    def finite(item):
        if type(item) is float:
            assert math.isfinite(item)
        elif type(item) is dict:
            for child in item.values():
                finite(child)
        elif type(item) is list:
            for child in item:
                finite(child)
    finite(value)
    return value


def source_bytes(busy):
    if busy:
        return bytes(((37*x+61*y+13*x*y+19*(x % 3)+23*(y % 5)) % 256
                      +(47 if x == 0 else 0)+(83 if y == 16 else 0)) % 256
                     for y in range(17) for x in range(23))
    return b"".join(struct.pack("<f", ((x+3*y) % 251)/16 +
                               (512 if (x, y) == (257, 193) else 0))
                    for y in range(512) for x in range(512))


def workspace():
    for parent in Path(__file__).resolve().parents:
        if (parent / "src/desktop/window.cpp").is_file() and (parent / "docs/PLAN.md").is_file():
            return parent
    pytest.fail("Cannot locate adopted job-cancellation contract; configure its explicit path")


def configured_pin(path_name, hash_name, expected):
    configured, digest = os.environ.get(path_name), os.environ.get(hash_name)
    assert configured and digest, f"Required cancellation evidence missing: {path_name}/{hash_name}"
    assert digest == expected
    data = Path(configured).resolve(strict=True).read_bytes()
    assert sha(data) == expected
    return data


@pytest.fixture(scope="module")
def jobcancel_evidence():
    assert os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0") in {"0", "1"}
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") != "1" and not os.environ.get(
            "IMAGEJXX_GAUSSIAN_REFERENCE_JSONL"):
        pytest.skip("Installed job-cancellation UI is optional outside a required desktop gate")
    reference = configured_pin("IMAGEJXX_GAUSSIAN_REFERENCE_JSONL",
                               "IMAGEJXX_GAUSSIAN_REFERENCE_SHA256", REFERENCE_SHA)
    numeric = decode(configured_pin("IMAGEJXX_GAUSSIAN_CONTRACT_JSON",
                                   "IMAGEJXX_GAUSSIAN_CONTRACT_SHA256", NUMERIC_CONTRACT_SHA))
    assert type(numeric["schema_version"]) is int and numeric["schema_version"] == 1
    configured = os.environ.get("IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON")
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
        assert configured, "Required cancellation gate must supply the adopted UI contract path"
    path = Path(configured) if configured else workspace() / (
        "build/plan-implementation/p02a-gaussian-command-001/shared-ui-case-contract-002.json")
    ui_bytes = path.resolve(strict=True).read_bytes()
    assert sha(ui_bytes) == UI_CONTRACT_SHA
    ui = decode(ui_bytes)
    adopted = [item for group in ui["cases"] if group["id"] == GROUP_ID
               for item in group["variants"] if item["id"] in VARIANTS]
    assert [item["id"] for item in adopted] == VARIANTS
    assert len(adopted) == 2 and sum(len(item["steps"]) for item in adopted) == 22
    assert sum(len(item["assertions"]) for item in adopted) == 9
    assert all(item["required_contexts"] == CONTEXTS for item in adopted)
    expected_u8, expected_f32 = source_bytes(True), source_bytes(False)
    assert sha(expected_u8) == U8_SOURCE_SHA and len(expected_u8) == 391
    assert sha(expected_f32) == F32_SOURCE_SHA and len(expected_f32) == 262144*4
    ids, selected = set(), None
    for line in reference.split(b"\n"):
        if not line.strip():
            continue
        row = decode(line)
        assert row["id"] not in ids
        ids.add(row["id"])
        if row["id"] != "u8-full-s2":
            continue
        assert row["dtype"] == "U8" and row["shape"] == [17, 23]
        assert row["input_formula"] == "owned-asymmetric-analytic-v1"
        raw_source = bytes.fromhex(row["input_raw_hex"])
        assert raw_source == bytes(row["input"]) == expected_u8
        assert sha(raw_source) == row["input_sha256"] == U8_SOURCE_SHA
        assert row["raw_reference_output_unmodified"] is True
        assert row["full_plugin_runner_executed"] is row["dialog_executed"] is False
        selected = ui["numeric_oracles"][row["id"]]
        assert selected["input_sha256"] == U8_SOURCE_SHA
    assert len(ids) == 332 and selected is not None
    owned = ui["owned_inputs"]["resource_f32_512x512"]
    assert owned["dtype"] == "F32" and owned["shape"] == [512, 512]
    assert owned["reference_observation"] is False
    return {"variants": {item["id"]: item for item in adopted},
            "u8": expected_u8, "f32": expected_f32, "input_oracle": selected}


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
    size, encoding = {"uint8": (1, "UInt8"), "float32": (4, "IEEE754 Float32LE")}[dtype]
    count = math.prod(shape)
    assert member["dtype"] == dtype and member["shape"] == shape and member["encoding"] == encoding
    assert integer(member["sample_count"]) == count and integer(member["byte_count"]) == count*size
    data = path.read_bytes()
    assert len(data) == count*size and sha(data) == member["sha256"]
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
        integer(job["document_id"], positive=True)
        assert job["state"] in {"QUEUED", "RUNNING", "READY", "PUBLISHING", "COMPLETED", "FAILED", "CANCELED"}
        assert type(job["label"]) is type(job["error"]) is type(job["stage"]) is str
        assert type(job["cancel_requested"]) is bool
        for field in ("resource_bytes", "completed", "total", "checkpoints", "stage_completed", "stage_total"):
            integer(job[field])
        result[job_id] = job
    return result


def single_job(checkpoint, source_id, *, state=None, canceled=None):
    actual = jobs(checkpoint["actual_native_jobs"], source_id)
    assert len(actual) == 1
    job = next(iter(actual.values()))
    if state is not None:
        assert job["state"] == state
    if canceled is not None:
        assert job["cancel_requested"] is canceled
    return job


def calibration(value):
    assert set(value) == set(CALIBRATION)
    for field, size in (("spacing", 3), ("origin", 3), ("direction", 9)):
        assert type(value[field]) is list and len(value[field]) == size
        assert all(type(item) in {int, float} and math.isfinite(item) for item in value[field])
    for field in ("frame_interval", "time_origin"):
        assert type(value[field]) in {int, float} and math.isfinite(value[field])
    assert type(value["unit"]) is type(value["time_unit"]) is str
    assert value == CALIBRATION


def document(value, directory, expected, *, busy):
    shape, dtype = ([17, 23], "uint8") if busy else ([512, 512], "float32")
    integer(value["document_id"], positive=True)
    integer(value["data_identity"])
    assert value["shape_tcz_yxs"] == [1, 1, 1, *shape, 1] and value["dtype"] == dtype
    for item in value["shape_tcz_yxs"]:
        integer(item, positive=True)
    assert type(value["dirty"]) is bool
    integer(value["undo_steps"])
    integer(value["redo_steps"])
    assert set(value["revisions"]) == {"pixels", "metadata", "selection", "display"}
    for item in value["revisions"].values():
        integer(item)
    calibration(value["calibration"])
    assert value["history"]["undo_steps"] == value["undo_steps"]
    assert value["history"]["redo_steps"] == value["redo_steps"]
    integer(value["history"]["step_limit"])
    integer(value["history"]["pixel_budget"])
    assert value["history"]["pixels"]["exact"] is True
    for field, item in value["history"]["pixels"].items():
        if field != "exact":
            integer(item)
    assert [item["position"] for item in value["planes"]] == [[0, 0, 0]]
    plane = value["planes"][0]
    for item in plane["position"]:
        integer(item)
    assert raw(plane["raw"], directory, dtype=dtype, shape=shape) == expected
    for field in ("image_address", "buffer_address"):
        assert type(plane[field]) is str and int(plane[field], 16) > 0
    info = value["document_info"]
    assert info["id"] == value["document_id"] and info["dtype"] == dtype
    assert info["dirty"] is value["dirty"] and info["revisions"] == value["revisions"]
    assert info["undo_steps"] == value["undo_steps"] and info["redo_steps"] == value["redo_steps"]
    assert info["axis_sizes"] == {"T": 1, "C": 1, "Z": 1, "Y": shape[0], "X": shape[1], "S": 1}
    for item in info["axis_sizes"].values():
        integer(item, positive=True)
    assert info["view"]["crosshair"] == value["display"]["crosshair"]


def native_state(value, directory, expected, *, busy):
    assert set(value["documents"]) == {"source"}
    source = value["documents"]["source"]
    document(source, directory, expected, busy=busy)
    assert value["active_document_id"] == source["document_id"]
    assert value["application_document_ids"] == value["desktop_document_ids"] == [source["document_id"]]
    integer(value["active_document_id"], positive=True)
    for item in value["application_document_ids"] + value["desktop_document_ids"]:
        integer(item, positive=True)
    assert value["recording"] is False and value["workflow"] == value["results"] == []
    return source


def unchanged(value, before, directory, expected, *, busy):
    source = native_state(value, directory, expected, busy=busy)
    assert set(value) == set(before)
    for field in before:
        if field != "documents":
            assert value[field] == before[field], f"Canceled work changed native {field}"
    original = before["documents"]["source"]
    assert set(source) == set(original)
    for field in original:
        if field != "planes":
            assert source[field] == original[field], f"Canceled work changed source {field}"
    for current, prior in zip(source["planes"], original["planes"]):
        assert {key: item for key, item in current.items() if key != "raw"} == {
            key: item for key, item in prior.items() if key != "raw"}


def cpu_readback(rendered, directory, expected, source_id, *, busy):
    shape, dtype = ([17, 23], "uint8") if busy else ([512, 512], "float32")
    ordinary = rendered["ordinary_pixels"]
    assert ordinary["pixels_source"] == ordinary["contrast_source"] == "live"
    assert ordinary["source_document_id"] == source_id and ordinary["preview_generation"] == 0
    integer(ordinary["source_document_id"], positive=True)
    integer(ordinary["preview_generation"])
    readback = ordinary["readback"]
    assert readback["available"] is True
    source, rgb = readback["source"], readback["rendered_rgb"]
    assert source["dtype"] == dtype and source["shape"] == [*shape, 1]
    assert rgb["dtype"] == "uint8" and rgb["shape"] == [*shape, 3]
    for image in (source, rgb):
        assert "pixels" not in image and "no GPU readback" in image["observation_scope"]
    assert raw(source["raw"], directory, dtype=dtype, shape=[*shape, 1]) == expected
    if busy:
        display = expected
    else:
        display = bytes(math.floor(min(255, max(0, sample[0]*(255/528))) + 0.5)
                        for sample in struct.iter_unpack("<f", expected))
    assert raw(rgb["raw"], directory, dtype="uint8", shape=[*shape, 3]) == bytes(
        channel for value in display for channel in (value, value, value))


def host(value):
    for field in ("admission_epoch", "current_epoch", "drained_epoch"):
        integer(value[field])
    for field in ("mirrored_ids", "actual_lane_ids"):
        assert len(value[field]) == 2
        for item in value[field]:
            integer(item)


def worker_barrier(value, source_id, *, busy):
    for field in ("entered", "released", "timed_out", "working_owner_observed"):
        assert type(value[field]) is bool
    assert value["timed_out"] is value["working_owner_observed"] is False
    assert value["working_owner"] is None
    assert "no invented working owner" in value["working_owner_scope"]
    integer(value["bound_job_id"])
    integer(value["completed"])
    integer(value["total"])
    events = value["events"]
    assert len(events) <= 4096
    assert [item["sequence"] for item in events] == list(range(1, len(events)+1))
    allowed = {"legacy_worker_entered", "worker_entered", "stage_progress",
               "real_worker_barrier_entered", "real_worker_barrier_released",
               "worker_release_requested", "submit_returned_job_association",
               "admission_before_submit", "admission_after_submit"}
    for event in events:
        assert event["phase"] in allowed and type(event["thread_id"]) is str and event["thread_id"]
        assert event["role"] in {"host", "native_worker"}
        if event["phase"] in {"worker_entered", "stage_progress"}:
            assert busy is False and event["role"] == "native_worker"
            assert event["operation"] == "gaussian_blur_imagej" and event["source_id"] == source_id
            integer(event["completed"])
            integer(event["total"])
            if event["phase"] == "worker_entered":
                assert event["stage"] == "" and event["completed"] == event["total"] == 0
            else:
                if event["stage"] == "Numeric arguments":
                    # NumericArray validates the two real sigma parameters before kernel work.
                    assert event["total"] == 2 and event["completed"] in {0, 2}
                else:
                    assert event["stage"].startswith("ImageJ Gaussian ")
                assert event["completed"] <= event["total"]
        elif event["phase"] == "legacy_worker_entered":
            assert busy is True and event["role"] == "native_worker" and event["operation"] == "add(value0)"
        elif event["phase"] == "submit_returned_job_association":
            assert event["role"] == "host" and event["job_id"] == value["bound_job_id"]
            assert event["association_timing"] == "after actual submission returned"
    return events


def actual_control(rendered, selector, job_id):
    if selector.startswith("gaussian_panel.controls."):
        name = selector.removeprefix("gaussian_panel.controls.")
        value = dict(rendered["gaussian_panel"]["control_states"][name])
        value["rect"] = rendered["gaussian_panel"]["control_rects"][name]
        value["point"] = rendered["gaussian_panel"]["controls"][name]
        return value
    assert selector == "jobs.active_gaussian.cancel"
    rows = [item for item in rendered["jobs_panel"]["jobs"] if item["id"] == job_id]
    assert len(rows) == 1
    value = dict(rows[0]["cancel"])
    value["native_job_id"], value["native_job_state"] = rows[0]["id"], rows[0]["state"]
    assert value["native_job_state"] == "Running"
    return value


def physical_frames(row, directory, adopted, source_id, *, busy):
    frames = row["frames"]
    assert frames and len(frames) <= 1200
    assert [item["ordinal"] for item in frames] == list(range(1, len(frames)+1))
    sessions, previous_events = [], []
    for frame in frames:
        assert decode(member_path(directory, f"frame-{frame['ordinal']}.json").read_bytes()) == frame
        usage(frame["usage"])
        assert len(jobs(frame["actual_native_jobs"], source_id)) <= 1
        host(frame["host"])
        assert type(frame["busy"]) is bool
        events = worker_barrier(frame["worker_barrier"], source_id, busy=busy)
        assert events[:len(previous_events)] == previous_events
        previous_events = events
        rendered = frame["rendered"]
        session = rendered["session"]
        assert session["open"] is True
        integer(session["session_id"], positive=True)
        integer(session["context_id"], positive=True)
        assert session["current_context_id"] == session["context_id"]
        sessions.append(session)
        actual = rendered["input_frame"]
        assert actual["raw_inputs"] == frame["requested_events"]
        for event in frame["requested_events"]:
            assert event["type"] in {"position", "button", "key", "keys", "text"}
            if event["type"] == "position":
                assert set(event) == {"type", "space", "point"} and event["space"] == "screen"
                assert actual["mouse_position"] == [math.floor(item) for item in event["point"]]
            elif event["type"] == "button":
                assert event["name"] == "left" and type(event["down"]) is bool
                assert actual["mouse_down"][0] is event["down"]
            elif event["type"] in {"key", "keys"}:
                atoms = event["keys"] if event["type"] == "keys" else [{"name": event["name"], "down": event["down"]}]
                for atom in atoms:
                    assert set(atom) == {"name", "down"} and type(atom["down"]) is bool
                    assert (atom["name"] in actual["keys_down"]) is atom["down"]
            else:
                assert type(event["value"]) is str
        assert rendered["gaussian_jobs"]["apply_callback_count"] == 0
        integer(rendered["gaussian_jobs"]["apply_callback_count"])
        assert rendered["gaussian_jobs"]["installed_preview_storage"] is None
    assert len({item["session_id"] for item in sessions}) == len({item["context_id"] for item in sessions}) == 1
    for before, after in zip(sessions, sessions[1:]):
        assert after["imgui_frame"] == before["imgui_frame"]+1 and after["imgui_time"] > before["imgui_time"]
    traces = row["steps"]
    assert [item["ordinal"] for item in traces] == list(range(1, len(adopted["steps"])+1))
    assert [item["adopted_step"] for item in traces] == adopted["steps"]
    previous_last = 0
    for trace in traces:
        first, last = integer(trace["first_frame_ordinal"]), integer(trace["last_frame_ordinal"])
        assert previous_last <= first <= last <= len(frames)
        previous_last = last
        assert trace["before_frame_ordinal"] == first and trace["completed"] is True
        if trace["adopted_step"]["op"] in {"observe", "real_worker_barrier", "api_preparation"}:
            assert first == last
        for frame in frames[first:last]:
            assert frame["step_ordinal"] == trace["ordinal"]
    return frames


def click_events(observation):
    return [[{"type": "position", "space": "screen", "point": observation["position"]}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}], []]


def physical_steps(row, frames, source_id, job_id, *, busy):
    by_step = {}
    for observation in row["control_observations"]:
        ordinal = integer(observation["step_ordinal"], positive=True)
        by_step.setdefault(ordinal, []).append(observation)
        first = integer(observation["before_frame_ordinal"], positive=True)
        selector, control = observation["selector"], observation["control"]
        integer(control["id"], positive=True)
        assert control == actual_control(frames[first-1]["rendered"], selector, job_id)
        assert type(control["enabled"]) is type(control["active"]) is type(control["focused"]) is bool
        disabled = busy and selector == "gaussian_panel.controls.ok"
        assert observation["attempted_disabled"] is disabled and control["enabled"] is (not disabled)
        x, y = observation["position"]
        assert x == math.floor(x) and y == math.floor(y)
        rect = control["rect"]
        assert len(rect) == 4 and all(type(item) in {int, float} and math.isfinite(item) for item in rect)
        assert rect[0] <= x < rect[2] and rect[1] <= y < rect[3]
        current = actual_control(frames[first]["rendered"], selector, job_id)
        assert current["id"] == control["id"] and current["enabled"] is control["enabled"]
        assert current["rect"][0] <= x < current["rect"][2] and current["rect"][1] <= y < current["rect"][3]
        assert [item["requested_events"] for item in frames[first:first+3]] == click_events(observation)[:3]
    for trace in row["steps"]:
        first, last, step = trace["first_frame_ordinal"], trace["last_frame_ordinal"], trace["adopted_step"]
        observations = by_step.get(trace["ordinal"], [])
        actual = [item["requested_events"] for item in frames[first:last]]
        if step["op"] == "click":
            assert len(observations) == 1 and observations[0]["selector"] == step["selector"]
            assert observations[0]["before_frame_ordinal"] == first and actual == click_events(observations[0])
        elif step["op"] == "replace_sigma":
            assert len(observations) == 1 and observations[0]["selector"] == "gaussian_panel.controls.sigma"
            assert observations[0]["before_frame_ordinal"] == first
            select_all = [[{"type": "keys", "keys": [{"name": "Ctrl", "down": down},
                                                      {"name": "A", "down": down}]}] for down in (True, False)]
            assert actual == click_events(observations[0]) + select_all + [[{"type": "text", "value": step["text"]}], []]
            focused = frames[first+3]["rendered"]["input_frame"]
            identifier = observations[0]["control"]["id"]
            assert focused["focus"]["active_id"] == focused["input_text"]["id"] == identifier
            assert frames[last-1]["rendered"]["gaussian_panel"]["sigma_text"] == step["text"]
        elif step["op"] == "key_pulse":
            assert observations == [] and actual == [[{"type": "key", "name": step["name"], "down": True}],
                                                     [{"type": "key", "name": step["name"], "down": False}], []]
        elif step["op"] == "open_command":
            assert observations == [] and actual == [[]]
            panel = frames[last-1]["rendered"]["gaussian_panel"]
            assert panel["open"] is panel["visible"] is True and panel["source_id"] == source_id
            assert panel["sigma"] == 2 and panel["sigma_text"] == "2"
            assert panel["preview"] is False
        else:
            assert observations == [] and all(events == [] for events in actual)


def checkpoints(row, directory, frames, source_id, *, busy):
    names = (["baseline", "application_running", "busy_attempt", "dialog_canceled", "canceled", "cleanup"] if busy else
             ["baseline", "positive_work", "before_jobs_cancel", "cancel_requested", "canceled", "cleanup"])
    assert [item["name"] for item in row["checkpoints"]] == names
    result, previous = {}, 0
    for item in row["checkpoints"]:
        assert decode(member_path(directory, "checkpoint-"+item["name"]+".json").read_bytes()) == item
        ordinal = integer(item["frame_ordinal"], positive=True)
        assert previous <= ordinal <= len(frames)
        previous = ordinal
        assert item["rendered"] == frames[ordinal-1]["rendered"]
        usage(item["usage"])
        host(item["host"])
        jobs(item["actual_native_jobs"], source_id)
        events = worker_barrier(item["worker_barrier"], source_id, busy=busy)
        frame_events = frames[ordinal-1]["worker_barrier"]["events"]
        assert events[:len(frame_events)] == frame_events
        result[item["name"]] = item
    for trace in row["steps"]:
        step = trace["adopted_step"]
        if step["op"] == "observe":
            item = result[step["label"]]
            assert trace["checkpoint_name"] == step["label"]
            assert item["frame_ordinal"] == trace["first_frame_ordinal"] == trace["last_frame_ordinal"]
            assert item["step_ordinal"] == trace["ordinal"]
        else:
            assert trace["checkpoint_name"] is None
    return result


def command_record(record, before):
    assert set(record) == {"schema_version", "operation", "options", "calibration", "effective_sigma_xy", "target"}
    assert type(record["schema_version"]) is int and record["schema_version"] == 1
    assert record["operation"] == "gaussian_blur_imagej"
    assert record["options"] == {"sigma": 2, "scaled_units": False}
    assert type(record["options"]["sigma"]) in {int, float} and record["options"]["scaled_units"] is False
    assert record["effective_sigma_xy"] == [2, 2] and record["calibration"] == before["calibration"]
    assert record["target"] == {"schema_version": 1, "document_id": before["document_id"],
        "source_revisions": [before["revisions"][field] for field in ("pixels", "metadata", "selection")],
        "source_shape": before["shape_tcz_yxs"], "scope": "ranges", "ranges": [[0, 1, 1]]*3,
        "positions": [], "roi_policy": "active_selection", "execution_mode": "planes", "destination": "in_place"}
    integer(record["target"]["document_id"], positive=True)
    assert type(record["target"]["schema_version"]) is int
    for values in [record["target"]["source_revisions"], record["target"]["source_shape"], *record["target"]["ranges"]]:
        for item in values:
            integer(item)
    calibration(record["calibration"])


def positive_boundary(checkpoint, source_id, job_id):
    job = single_job(checkpoint, source_id, state="RUNNING", canceled=False)
    assert job["id"] == job_id and job["stage"] == COPY_STAGE
    assert job["stage_completed"] == 1024 and job["stage_total"] == 262144
    integer(job["resource_bytes"], positive=True)
    gate = checkpoint["worker_barrier"]
    assert gate["entered"] is True and gate["released"] is gate["timed_out"] is False
    assert gate["observed_phase"] == "stage_progress" and gate["observed_stage"] == COPY_STAGE
    assert gate["completed"] == 1024 and gate["total"] == 262144 and gate["bound_job_id"] == job_id
    assert checkpoint["busy"] is True and checkpoint["usage"]["running"] == 1
    assert checkpoint["usage"]["queued"] == checkpoint["usage"]["retained_bytes"] == 0
    integer(checkpoint["usage"]["running_bytes"], positive=True)
    assert checkpoint["host"]["actual_lane_ids"][1] == checkpoint["host"]["mirrored_ids"][1] == job_id


def barrier_order(row, source_id, job_id, *, busy):
    gate = row["worker_barrier"]
    events = worker_barrier(gate, source_id, busy=busy)
    assert gate["entered"] is gate["released"] is True and gate["bound_job_id"] == job_id
    def one(phase):
        values = [item for item in events if item["phase"] == phase]
        assert len(values) == 1, f"Missing/repeated actual native barrier phase: {phase}"
        return values[0]
    association, entered, release, released = [one(phase) for phase in (
        "submit_returned_job_association", "real_worker_barrier_entered", "worker_release_requested", "real_worker_barrier_released")]
    assert association["job_id"] == job_id
    assert entered["sequence"] < release["sequence"] < released["sequence"]
    assert association["sequence"] < release["sequence"]
    assert entered["role"] == released["role"] == "native_worker" and release["role"] == "host"
    assert entered["thread_id"] == released["thread_id"] != association["thread_id"] == release["thread_id"]
    assert release["reason"] == "adopted literal release step"
    known = integer(entered["job_id_known_at_observation"])
    if entered["sequence"] < association["sequence"]:
        assert known == 0, "Early worker event must not invent a submit-returned job identity"
    else:
        assert known == job_id
    if busy:
        native = one("legacy_worker_entered")
        assert native["sequence"] < entered["sequence"]
        assert entered["observed_phase"] == "legacy_worker_entered" and entered["observed_stage"] == ""
        assert entered["completed"] == entered["total"] == gate["completed"] == gate["total"] == 0
        assert not any(item["phase"].startswith("admission_") for item in events)
    else:
        native = one("worker_entered")
        positive = [item for item in events if item["phase"] == "stage_progress" and
                    item["stage"] == COPY_STAGE and item["completed"] == 1024 and item["total"] == 262144]
        assert len(positive) == 1 and native["sequence"] < positive[0]["sequence"] < entered["sequence"]
        admission = [one(phase) for phase in ("admission_before_submit", "admission_after_submit")]
        assert admission[0]["sequence"] < admission[1]["sequence"] < association["sequence"]
        assert admission[0]["actual_lane_ids"][1] == admission[0]["mirrored_ids"][1] == 0
        assert admission[1]["actual_lane_ids"][1] == job_id
        assert all(item["admission_epoch"] == item["current_epoch"] for item in admission)
    assert not any(item["phase"] in {"before_native_publish", "real_worker_barrier_timeout"} for item in events)


def api_inputs(row, source_id, job_id, *, busy):
    values = row["api_inputs"]
    purposes = [item["purpose"] for item in values]
    expected = ["explicit owned finite fixture/default preparation only", "diagnostic Jobs visibility preparation"]
    if busy:
        expected += ["adopted real-worker barrier preparation", "adopted submit_genuine_job_without_publication",
                     "adopted busy labelled_action_preparation opening only", "adopted unrelated preparation cancellation",
                     "adopted real-worker barrier release"]
    else:
        expected += ["adopted labelled_action_preparation opening only", "adopted real-worker barrier preparation",
                     "adopted real-worker barrier release"]
    expected += ["bounded final/failure native-job cleanup only", "bounded owned-fixture teardown after atomicity assertions"]
    assert purposes == expected
    for value in values:
        integer(value["step_ordinal"])
        integer(value["frame_ordinal"])
    prep = values[0]
    assert prep["step_ordinal"] == prep["frame_ordinal"] == 0 and prep["document_id"] == source_id
    assert prep["methods"] == ["addNativeDocument", "setNativeDisplay", "mark_saved"]
    assert prep["recording"] is prep["tested_changes_use_api_setters"] is False
    assert values[1]["action"] == "show_jobs" and values[1]["parameters"] == {"visible": True}
    opening = next(item for item in values if "opening only" in item["purpose"])
    assert opening["opening_is_gesture_proof"] is False
    if busy:
        assert opening["method"] == "GaussianCancellationAccess.showBusyGaussian"
        assert opening["operation"] == "gaussian_blur_imagej"
        blocker = next(item for item in values if item["purpose"] == "adopted submit_genuine_job_without_publication")
        assert blocker["method"] == "GaussianCancellationAccess.startBusyPreparation"
        assert blocker["operation"] == "add" and blocker["parameters"] == {"value": 0}
        assert blocker["job_id"] == job_id and blocker["job_document_id"] == source_id
        assert blocker["unrelated_to_gaussian_apply"] is True
        cancel = next(item for item in values if item["purpose"] == "adopted unrelated preparation cancellation")
        assert cancel["method"] == "Window.cancelJob" and cancel["job_id"] == job_id and cancel["accepted"] is True
    else:
        assert opening["action"] == "show_operation" and opening["parameters"] == {"operation": "gaussian_blur_imagej"}
    cleanup, teardown = values[-2:]
    assert cleanup["methods"] == ["release actual worker barrier", "Window.cancel", "actual job wait_for5000ms",
                                   "Window.poll", "drop native job copies"]
    assert teardown["methods"] == ["closeDocument(discard=true)", "drop original snapshot/document aliases"]
    assert teardown["document_id"] == source_id
    assert not any("set_sigma" in json.dumps(item) or "set_preview" in json.dumps(item) for item in values)


def assert_row(row, variant, context, directory, evidence):
    busy = variant == VARIANTS[0]
    expected = evidence["u8" if busy else "f32"]
    shape, dtype = ([17, 23], "uint8") if busy else ([512, 512], "float32")
    assert decode(member_path(directory, "case.json").read_bytes()) == row
    assert type(row["schema_version"]) is int and row["schema_version"] == 1
    assert row["group_id"] == GROUP_ID and row["case_id"] == variant and row["context"] == context
    assert row["status"] == "passed" and row["acceptance_claim"] is False
    assert row["contract_sha256"] == UI_CONTRACT_SHA and row["adopted_variant"] == evidence["variants"][variant]
    assert all(row[field] is False for field in SCOPE_FLAGS)
    assert raw(row["fixture_input_raw"], directory, dtype=dtype, shape=shape) == expected
    before = row["before"]
    source = native_state(before, directory, expected, busy=busy)
    source_id = source["document_id"]
    assert source["dirty"] is False and source["undo_steps"] == source["redo_steps"] == 0
    assert source["selection"] is source["previous_selection"] is None and source["spectrum_present"] is False
    assert source["metadata"] == {"title": "Owned group20 finite source", "properties": {"jobcancel_property": "preserve"},
                                  "extensions": {"jobcancel_extension": "unknown-kept"}, "record_layout": {}}
    assert len(source["rois"]) == len(source["overlays"]) == 1
    roi = source["rois"][0]
    assert roi["name"] == "retained-jobcancel-roi" and roi["attributes"] == {"jobcancel_unknown": "roi-kept"}
    assert roi["t"] is roi["c"] is None and roi["z"] == 0
    assert roi["selection"] == {"kind": "rectangle", "points": [[3, 2], [8, 7]], "width": 1,
                                "jobcancel_shape_unknown": "shape-kept"}
    assert source["overlays"] == [{"kind": "point", "color": "#ffcc0000", "points": [[9, 13]],
                                    "jobcancel_unknown": "overlay-kept"}]
    assert source["display"]["t"] == source["display"]["c"] == 0
    assert source["display"]["crosshair"] == ([11, 8, 0] if busy else [257, 193, 0])
    assert source["display"]["orthogonal"] is False
    assert source["display"]["properties"]["display_range"] == [0, 255 if busy else 528]
    if busy:
        assert row["reference_capture_sha256"] == REFERENCE_SHA and row["input_oracle"] == evidence["input_oracle"]
    else:
        assert row["owned_input"] == {"id": "resource_f32_512x512", "reference_observation": False,
                                      "expected_source_sha256": F32_SOURCE_SHA}
    frames = physical_frames(row, directory, evidence["variants"][variant], source_id, busy=busy)
    observed = checkpoints(row, directory, frames, source_id, busy=busy)
    baseline = observed["baseline"]
    assert baseline["native_state"] == before and baseline["busy"] is False
    usage(baseline["usage"], empty=True)
    assert baseline["actual_native_jobs"] == []
    terminal = row["terminal_job"]
    job_id = integer(terminal["id"], positive=True)
    assert jobs([terminal], source_id)[job_id]["state"] == "CANCELED" and terminal["cancel_requested"] is True
    physical_steps(row, frames, source_id, job_id, busy=busy)
    for checkpoint in observed.values():
        unchanged(checkpoint["native_state"], before, directory, expected, busy=busy)
        cpu_readback(checkpoint["rendered"], directory, expected, source_id, busy=busy)
        controller = checkpoint["rendered"]["gaussian_jobs"]
        assert controller["apply_callback_count"] == 0 and controller["installed_preview_storage"] is None
        integer(controller["apply_callback_count"])
        resources = controller["resource_usage"]
        assert resources["reserved_bytes"] == checkpoint["usage"]["reserved_bytes"]
        assert resources["running_reserved_bytes"] == checkpoint["usage"]["running_bytes"]
        assert resources["retained_reserved_bytes"] == checkpoint["usage"]["retained_bytes"] == 0
    unchanged(row["after"], before, directory, expected, busy=busy)
    for field in ("original_after_raw", "original_after_cleanup_raw"):
        assert raw(row[field], directory, dtype=dtype, shape=shape) == expected
    publication = row["publication_observation"]
    assert publication == {"before_native_publish_probe_count": 0, "actual_apply_callback_count": 0,
                           "published_document_id": None, "live_destination_document_id": source_id,
                           "working_owner_observed": False, "working_owner": None}
    canceled = observed["canceled"]
    assert single_job(canceled, source_id, state="CANCELED", canceled=True)["id"] == job_id
    assert canceled["busy"] is False
    usage(canceled["usage"], empty=True)
    usage(observed["cleanup"]["usage"], empty=True)
    barrier_order(row, source_id, job_id, busy=busy)
    api_inputs(row, source_id, job_id, busy=busy)
    if busy:
        running, attempt, closed = [observed[name] for name in ("application_running", "busy_attempt", "dialog_canceled")]
        for checkpoint in (running, attempt, closed):
            job = single_job(checkpoint, source_id, state="RUNNING", canceled=False)
            assert job["id"] == job_id and checkpoint["busy"] is True
            integer(job["resource_bytes"], positive=True)
            assert checkpoint["usage"]["running"] == 1 and checkpoint["usage"]["queued"] == 0
            integer(checkpoint["usage"]["running_bytes"], positive=True)
            assert checkpoint["worker_barrier"]["entered"] is True and checkpoint["worker_barrier"]["released"] is False
            assert checkpoint["worker_barrier"]["observed_phase"] == "legacy_worker_entered"
            assert checkpoint["rendered"]["gaussian_jobs"]["apply"]["job"] is None
            assert checkpoint["rendered"]["gaussian_jobs"]["preview"]["job"] is None
        panel = attempt["rendered"]["gaussian_panel"]
        assert panel["open"] is panel["visible"] is panel["draft_valid"] is panel["apply_busy"] is True
        assert panel["sigma_text"] == "0.5" and panel["sigma"] == 0.5
        assert panel["control_states"]["sigma"]["enabled"] is panel["control_states"]["cancel"]["enabled"] is True
        assert panel["control_states"]["ok"]["enabled"] is False
        assert closed["rendered"]["gaussian_panel"]["open"] is False
        assert closed["rendered"]["gaussian_panel"]["exit_reason"] == "cancel"
        assert canceled["rendered"]["gaussian_panel"]["remembered"] == baseline["rendered"]["gaussian_panel"]["remembered"]
        assert baseline["rendered"]["gaussian_panel"]["remembered"] == {"sigma": 2, "scaled_units": False}
        assert [item["name"] for item in row["job_observations"]] == ["unrelated_preparation_running", "actual_native_finalized_canceled"]
    else:
        for name in ("positive_work", "before_jobs_cancel"):
            positive_boundary(observed[name], source_id, job_id)
        requested = observed["cancel_requested"]
        job = single_job(requested, source_id, state="RUNNING", canceled=True)
        assert job["id"] == job_id and job["stage"] == COPY_STAGE
        assert job["stage_completed"] == 1024 and job["stage_total"] == 262144
        assert requested["worker_barrier"]["entered"] is True and requested["worker_barrier"]["released"] is False
        integer(requested["usage"]["running_bytes"], positive=True)
        prior, after = row["physical_cancel_before_job"], row["physical_cancel_after_job"]
        assert prior["id"] == after["id"] == job_id and prior["state"] == after["state"] == "RUNNING"
        assert prior["cancel_requested"] is False and after["cancel_requested"] is True
        assert prior["stage_completed"] == after["stage_completed"] == 1024
        lane = canceled["rendered"]["gaussian_jobs"]["apply"]
        assert lane["owns_job"] is False and lane["job"]["id"] == job_id and lane["job"]["state"] == "canceled"
        assert lane["source_id"] == source_id and lane["published_document_id"] is None
        command_record(lane["command_record"], source)
        assert lane["target"] == lane["command_record"]["target"]
        assert [item["name"] for item in row["job_observations"]] == ["actual_gaussian_submitted", "genuine_positive_source_copy", "actual_native_finalized_canceled"]
    for observation in row["job_observations"]:
        assert jobs([observation["job"]], source_id).keys() == {job_id}
        if "usage" in observation:
            usage(observation["usage"])
    final = row["final_cleanup"]
    usage(final["usage"], empty=True)
    assert single_job(final, source_id, state="CANCELED", canceled=True)["id"] == job_id
    assert final["observer_retained_result_aliases"] == 0
    integer(final["observer_retained_result_aliases"])
    assert final["worker_barrier_expired"] is final["native_source_owner_expired"] is True
    assert final["application_document_ids"] == [] and final["desktop_document_count"] == 0
    integer(final["desktop_document_count"])
    assert [item["label"] for item in row["lifecycle"]] == ["opened", "before_close", "closed"]
    opened, before_close, closed = [item["observed"] for item in row["lifecycle"]]
    assert opened["open"] is before_close["open"] is True and closed["open"] is False
    assert opened["session_id"] == before_close["session_id"] == closed["session_id"]
    assert opened["context_id"] == before_close["context_id"] == frames[0]["rendered"]["session"]["context_id"]
    assert closed["context_id"] == closed["current_context_id"] == 0 and final["lifecycle"] == closed


@pytest.fixture(scope="module")
def jobcancel_cpp_report():
    configured = os.environ.get("IMAGEJXX_JOBCANCEL_CPP_REPORT")
    if not configured:
        if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
            pytest.fail("Required installed standalone report missing: IMAGEJXX_JOBCANCEL_CPP_REPORT")
        pytest.skip("Standalone job-cancellation report is optional outside a required desktop gate")
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
    return decode(editor._gaussian_jobcancel_case(variant=variant, context="imgui_python", directory=str(directory)))


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("variant", VARIANTS)
def test_installed_gaussian_jobcancel_ui(variant, context, request, tmp_path, jobcancel_evidence):
    if context == "imgui_cpp":
        report, root = request.getfixturevalue("jobcancel_cpp_report")
        row = next(item for item in report["cases"] if item["case_id"] == variant)
        directory = root / f"case{VARIANTS.index(variant)+1:02}"
    else:
        directory = tmp_path / "run"
        assert not directory.exists()
        row = python_row(variant, directory)
    assert_row(row, variant, context, directory, jobcancel_evidence)
