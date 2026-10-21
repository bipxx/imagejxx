"""Independent installed Gaussian Preview observations, adopted groups07--13.

Expected source bytes come from an owned analytic fixture; exact output bytes
come from pinned unmodified reference capture002. Producer PASS text, primitive
output, declared release counts and allocator labels are never pixel/lifetime
oracles. This module does not qualify upstream dialogs or GPU readback.
"""

import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import struct

import pytest


GROUPS = {
    "P02a.shared_ui.07.preview-initial-and-on": ["current-plane"],
    "P02a.shared_ui.08.preview-generation-supersession": ["sigma2-0p5-12"],
    "P02a.shared_ui.09.preview-invalid-recovery": ["nan", "negative"],
    "P02a.shared_ui.10.preview-off": ["release-ready-result"],
    "P02a.shared_ui.11.dialog-cancel-inputs": ["button", "escape", "titlebar", "focused-cancel-enter"],
    "P02a.shared_ui.12.captured-navigation": ["z0-captured-z2-visible"],
    "P02a.shared_ui.13.source-lifetime-staleness": ["actual-image-close", "pixels", "selection", "calibration"],
}
VARIANTS = [group+"."+suffix for group, suffixes in GROUPS.items() for suffix in suffixes]
CONTEXTS = ["imgui_cpp", "imgui_python"]
UI_CONTRACT_SHA = "2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255"
NUMERIC_CONTRACT_SHA = "3b3f040e01b93642d2b0d3d84db9574f7a9ee525b3a7f29dac7ad21ea03ea6c3"
REFERENCE_SHA = "8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762"
INPUT_SHA = "f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4"
OUTPUT_HASHES = {
    "u8-full-s0p5": "c8e467e214f036adec4f1ff169bfa41091acbc17d30e922974b751feec632a53",
    "u8-full-s2": "3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b",
    "u8-full-s12": "0693443fe207a6841cd12caf9a97ea811d3995193904dd0464993041119cfc76",
}
CALIBRATION = {"spacing": [1, 1, 1], "origin": [0, 0, 0],
               "direction": [1, 0, 0, 0, 1, 0, 0, 0, 1], "unit": "pixels",
               "frame_interval": 1, "time_origin": 0, "time_unit": "s"}
SHAPE = [17, 23]
SAMPLES = 17*23


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


def fixture_bytes():
    return bytes(((37*x+61*y+13*x*y+19*(x % 3)+23*(y % 5)) % 256
                  +(47 if x == 0 else 0)+(83 if y == 16 else 0)) % 256
                 for y in range(17) for x in range(23))


def workspace():
    for parent in Path(__file__).resolve().parents:
        if (parent / "src/desktop/window.cpp").is_file() and (parent / "docs/PLAN.md").is_file():
            return parent
    pytest.fail("Cannot locate adopted Preview UI contract; configure its explicit path")


def configured_pin(path_name, hash_name, expected):
    configured, digest = os.environ.get(path_name), os.environ.get(hash_name)
    assert configured and digest, f"Required Preview evidence missing: {path_name}/{hash_name}"
    assert digest == expected
    path = Path(configured).resolve(strict=True)
    data = path.read_bytes()
    assert sha(data) == expected
    return data


@pytest.fixture(scope="module")
def preview_oracles():
    assert os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0") in {"0", "1"}
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") != "1" and not os.environ.get(
            "IMAGEJXX_GAUSSIAN_REFERENCE_JSONL"):
        pytest.skip("Groups07--13 installed UI is optional outside a required desktop gate")
    reference = configured_pin("IMAGEJXX_GAUSSIAN_REFERENCE_JSONL",
                               "IMAGEJXX_GAUSSIAN_REFERENCE_SHA256", REFERENCE_SHA)
    numeric = decode(configured_pin("IMAGEJXX_GAUSSIAN_CONTRACT_JSON",
                                    "IMAGEJXX_GAUSSIAN_CONTRACT_SHA256", NUMERIC_CONTRACT_SHA))
    assert type(numeric["schema_version"]) is int and numeric["schema_version"] == 1
    configured = os.environ.get("IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON")
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
        assert configured, "Required Preview gate must supply IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON"
    path = Path(configured) if configured else workspace() / (
        "build/plan-implementation/p02a-gaussian-command-001/shared-ui-case-contract-002.json")
    ui_raw = path.resolve(strict=True).read_bytes()
    assert sha(ui_raw) == UI_CONTRACT_SHA
    ui = decode(ui_raw)
    variants = [v for group in ui["cases"] if group["id"] in GROUPS for v in group["variants"]]
    assert [item["id"] for item in variants] == VARIANTS
    assert len(variants) == 14 and sum(len(item["steps"]) for item in variants) == 129
    assert all(item["required_contexts"] == CONTEXTS for item in variants)
    original = fixture_bytes()
    assert len(original) == SAMPLES and sha(original) == INPUT_SHA
    selected, ids = {}, set()
    for line in reference.split(b"\n"):
        if not line.strip():
            continue
        record = decode(line)
        assert record["id"] not in ids
        ids.add(record["id"])
        if record["id"] not in OUTPUT_HASHES:
            continue
        assert record["shape"] == SHAPE and record["dtype"] == "U8"
        assert record["input_formula"] == "owned-asymmetric-analytic-v1"
        source, output = bytes.fromhex(record["input_raw_hex"]), bytes.fromhex(record["output_raw_hex"])
        assert source == struct.pack("<"+"B"*len(record["input"]), *record["input"]) == original
        assert output == struct.pack("<"+"B"*len(record["output"]), *record["output"]) and len(output) == SAMPLES
        assert sha(output) == record["output_sha256"] == OUTPUT_HASHES[record["id"]]
        assert record["raw_reference_output_unmodified"] is True
        assert record["roi"]["processor_bounds"] == [0, 0, 23, 17]
        assert record["roi"]["kind"] == "none" and record["roi"]["is_area"] is False
        # There are independently zero pixels outside a full-plane rectangle.
        # Null diagnostics are accepted only for this proven empty exterior.
        exterior = [index for index in range(SAMPLES)
                    if not (0 <= index % 23 < 23 and 0 <= index // 23 < 17)]
        changes = [index for index in exterior if source[index] != output[index]]
        assert changes == [] and record["outside_changed_pixels"] == len(changes) == 0
        assert record["outside_changes"] in ([], None)
        assert record["full_plugin_runner_executed"] is record["dialog_executed"] is False
        oracle = ui["numeric_oracles"][record["id"]]
        assert oracle["input_sha256"] == INPUT_SHA
        assert oracle["output_sha256"] == oracle["owned_expected_output_sha256"] == sha(output)
        assert oracle["raw_reference_full_output_equals_owned_expected"] is True
        assert oracle["raw_reference_outside_changed_pixels"] == 0
        selected[record["id"]] = {"definition": oracle, "output": output}
    assert len(ids) == 332 and set(selected) == set(OUTPUT_HASHES)
    return {"input": original, "oracles": selected, "variants": {item["id"]: item for item in variants}}


def member_path(directory, name):
    root = directory.resolve(strict=True)
    assert type(name) is str and name
    path = (root / name).resolve(strict=True)
    assert path.is_relative_to(root) and path.is_file()
    return path


def raw(member, directory, *, shape=SHAPE):
    path = member_path(directory, member["member_id"])
    configured = Path(member["path"])
    if not configured.is_absolute():
        configured = directory / configured
    assert configured.resolve(strict=True) == path
    count = math.prod(shape)
    assert member["shape"] == shape and member["dtype"] == "uint8" and member["encoding"] == "UInt8"
    assert type(member["sample_count"]) is type(member["byte_count"]) is int
    assert member["sample_count"] == member["byte_count"] == count
    data = path.read_bytes()
    assert len(data) == count and sha(data) == member["sha256"]
    return data


def usage(value, *, empty=False):
    assert set(value) == {"running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"}
    for item in value.values():
        integer(item)
    assert value["reserved_bytes"] == value["running_bytes"]+value["retained_bytes"]
    if empty:
        assert not any(value.values())


def jobs(value):
    assert type(value) is list
    result = {}
    for job in value:
        job_id = integer(job["id"], positive=True)
        assert job_id not in result
        integer(job["document_id"], positive=True)
        assert type(job["label"]) is type(job["error"]) is type(job["stage"]) is str
        assert type(job["cancel_requested"]) is bool
        assert job["state"] in {"QUEUED", "RUNNING", "READY", "PUBLISHING", "COMPLETED", "FAILED", "CANCELED"}
        for field in ("resource_bytes", "completed", "total", "checkpoints", "stage_completed", "stage_total"):
            integer(job[field])
        result[job_id] = job
    return result


def calibration(value):
    assert set(value) == set(CALIBRATION)
    for field, size in (("spacing", 3), ("origin", 3), ("direction", 9)):
        assert type(value[field]) is list and len(value[field]) == size
        assert all(type(item) in {int, float} and math.isfinite(item) for item in value[field])
    assert all(item > 0 for item in value["spacing"])
    for field in ("frame_interval", "time_origin"):
        assert type(value[field]) in {int, float} and math.isfinite(value[field])
    assert value["frame_interval"] > 0
    assert type(value["unit"]) is type(value["time_unit"]) is str


def document(value, directory, expected_planes, *, closed=False):
    integer(value["document_id"], positive=True)
    integer(value["data_identity"])
    assert value["shape_tcz_yxs"] == [1, 1, len(expected_planes), 17, 23, 1]
    assert value["dtype"] == "uint8" and type(value["dirty"]) is bool
    assert value["source_closed"] is closed
    assert value["application_member"] is (not closed)
    assert value["desktop_member"] is (not closed)
    integer(value["undo_steps"])
    integer(value["redo_steps"])
    assert set(value["revisions"]) == {"pixels", "metadata", "selection", "display"}
    for item in value["revisions"].values():
        integer(item)
    calibration(value["calibration"])
    assert value["history"]["undo_steps"] == value["undo_steps"]
    assert value["history"]["redo_steps"] == value["redo_steps"]
    assert value["history"]["pixels"]["exact"] is True
    for key, item in value["history"]["pixels"].items():
        if key != "exact":
            integer(item)
    assert [plane["position"] for plane in value["planes"]] == [[0, 0, z] for z in range(len(expected_planes))]
    for plane, expected in zip(value["planes"], expected_planes):
        assert raw(plane["raw"], directory) == expected
        for field in ("image_address", "buffer_address"):
            assert type(plane[field]) is str and int(plane[field], 16) > 0
    if closed:
        assert value["document_info"] is None
    else:
        info = value["document_info"]
        assert info["id"] == value["document_id"] and info["dtype"] == "uint8"
        assert info["dirty"] is value["dirty"]
        assert info["undo_steps"] == value["undo_steps"] and info["redo_steps"] == value["redo_steps"]
        assert info["view"]["crosshair"] == value["display"]["crosshair"]


def global_state(value, *, closed=False):
    integer(value["active_document_id"], positive=not closed)
    assert type(value["recording"]) is bool
    assert set(value["documents"]) == {"source"}
    for field in ("application_document_ids", "desktop_document_ids"):
        assert type(value[field]) is list and len(value[field]) == len(set(value[field]))
        for item in value[field]:
            integer(item, positive=True)
    source_id = value["documents"]["source"]["document_id"]
    expected_ids = [] if closed else [source_id]
    assert value["application_document_ids"] == value["desktop_document_ids"] == expected_ids
    assert value["active_document_id"] == (0 if closed else source_id)
    assert type(value["workflow"]) is type(value["results"]) is list


def seeded(value, directory, original_planes):
    document(value, directory, original_planes)
    assert value["calibration"] == CALIBRATION
    assert value["dirty"] is False and value["undo_steps"] == value["redo_steps"] == 0
    assert value["selection"] is value["previous_selection"] is None
    assert value["rois"] == value["overlays"] == [] and value["spectrum_present"] is False
    assert value["metadata"] == {"title": "Owned Gaussian preview source", "properties": {},
                                 "extensions": {}, "record_layout": {}}
    assert value["display"]["properties"]["display_range"] == [0, 255]
    assert value["display"]["t"] == value["display"]["c"] == 0
    assert value["display"]["crosshair"] == [11, 8, 0]
    assert value["display"]["orthogonal"] is False


def same_state(value, baseline, directory, expected_planes, *, navigation=False, closed=False):
    document(value, directory, expected_planes, closed=closed)
    assert set(value) == set(baseline)
    skip = {"planes"}
    if navigation:
        skip |= {"display", "revisions", "document_info"}
        assert {key: item for key, item in value["revisions"].items() if key != "display"} == {
            key: item for key, item in baseline["revisions"].items() if key != "display"}
        assert {key: item for key, item in value["display"].items() if key not in {"crosshair", "revision"}} == {
            key: item for key, item in baseline["display"].items() if key not in {"crosshair", "revision"}}
        assert value["display"]["crosshair"][:2] == baseline["display"]["crosshair"][:2]
    if closed:
        skip |= {"document_info", "source_closed", "application_member", "desktop_member"}
    for key in baseline:
        if key not in skip:
            assert value[key] == baseline[key], f"Preview changed committed native {key}"
    for actual, before in zip(value["planes"], baseline["planes"]):
        assert {key: item for key, item in actual.items() if key != "raw"} == {
            key: item for key, item in before.items() if key != "raw"}


def cpu_readback(checkpoint, directory, expected, *, preview=False, generation=None):
    ordinary = checkpoint["rendered"]["ordinary_pixels"]
    assert ordinary["pixels_source"] == ("preview" if preview else "live")
    assert ordinary["contrast_source"] == "live"
    integer(ordinary["source_document_id"], positive=True)
    assert ordinary["source_document_id"] == checkpoint["native_state"]["documents"]["source"]["document_id"]
    assert ordinary["preview_generation"] == (generation if preview else 0)
    assert preview is (generation is not None)
    readback = ordinary["readback"]
    assert readback["available"] is True
    source, rgb = readback["source"], readback["rendered_rgb"]
    assert source["dtype"] == "uint8" and source["shape"] == [17, 23, 1]
    assert rgb["dtype"] == "uint8" and rgb["shape"] == [17, 23, 3]
    assert "pixels" not in source and "pixels" not in rgb
    assert "no GPU readback" in source["observation_scope"] and "no GPU readback" in rgb["observation_scope"]
    assert raw(source["raw"], directory, shape=[17, 23, 1]) == expected
    assert raw(rgb["raw"], directory, shape=[17, 23, 3]) == bytes(channel for sample in expected for channel in (sample,)*3)


def expected_record(record, before, sigma, *, apply=False):
    assert set(record) == {"schema_version", "operation", "options", "calibration", "effective_sigma_xy", "target"}
    assert type(record["schema_version"]) is int and record["schema_version"] == 1
    assert record["operation"] == "gaussian_blur_imagej"
    assert record["options"] == {"sigma": sigma, "scaled_units": False}
    assert type(record["options"]["sigma"]) in {int, float}
    assert record["options"]["scaled_units"] is False
    assert record["calibration"] == before["calibration"] == CALIBRATION
    calibration(record["calibration"])
    assert record["effective_sigma_xy"] == [sigma, sigma]
    assert all(type(item) in {int, float} and math.isfinite(item) for item in record["effective_sigma_xy"])
    target = record["target"]
    assert target == {"schema_version": 1, "document_id": before["document_id"],
                      "source_revisions": [before["revisions"][key] for key in ("pixels", "metadata", "selection")],
                      "source_shape": before["shape_tcz_yxs"], "scope": "ranges",
                      "ranges": [[0, 1, 1], [0, 1, 1], [0, 1, 1]], "positions": [],
                      "roi_policy": "active_selection", "execution_mode": "planes", "destination": "in_place"}
    assert type(target["schema_version"]) is int
    integer(target["document_id"], positive=True)
    for values in [target["source_revisions"], target["source_shape"], *target["ranges"]]:
        for item in values:
            integer(item)
    assert type(apply) is bool


def storage(value, *, job_id, generation, source_id, z_count):
    assert type(value["schema_version"]) is int and value["schema_version"] == 1
    assert value["eager"] is True and value["allocator_ids"] is False
    assert value["native_sample_storage"] == "double" and value["native_sample_bytes"] == 8
    assert value["measurement_scope"] == "held_preview_eager_buffers_including_shared_source_planes"
    assert value["address_semantics"] == "process_lifetime_local"
    assert value["pixel_read_policy"] == "no_pixel_value_or_virtual_plane_reads"
    assert value["source_document_id"] == source_id and value["job_id"] == job_id
    assert value["request_generation"] == generation
    assert len(value["planes"]) == z_count and [plane["index"] for plane in value["planes"]] == list(range(z_count))
    images, buffers, guards = set(), {}, set()
    for plane in value["planes"]:
        assert [plane["width"], plane["height"], plane["channels"]] == [23, 17, 1]
        assert plane["guard_tag_present"] is True
        for field in ("image_address", "buffer_address", "guard_address"):
            assert type(plane[field]) is str and int(plane[field], 16) > 0
        assert plane["pixel_count"] == SAMPLES
        integer(plane["pixel_capacity"], positive=True)
        assert plane["pixel_capacity"] >= SAMPLES
        assert plane["pixel_payload_bytes"] == SAMPLES*8
        assert plane["pixel_capacity_bytes"] == plane["pixel_capacity"]*8
        images.add(plane["image_address"])
        buffers[plane["buffer_address"]] = (plane["pixel_payload_bytes"], plane["pixel_capacity_bytes"])
        guards.add(plane["guard_address"])
    assert value["unique_image_count"] == len(images) and value["unique_buffer_count"] == len(buffers)
    assert value["pixel_payload_bytes"] == sum(pair[0] for pair in buffers.values())
    assert value["pixel_capacity_bytes"] == sum(pair[1] for pair in buffers.values())
    assert value["guard_addresses"] and len(value["guard_addresses"]) == len(set(value["guard_addresses"]))
    assert set(value["guard_addresses"]) == guards
    return guards


def actual_control(rendered, selector):
    if selector.startswith("gaussian_panel.controls."):
        name = selector.removeprefix("gaussian_panel.controls.")
        value = dict(rendered["gaussian_panel"]["control_states"][name])
        value["rect"] = rendered["gaussian_panel"]["control_rects"][name]
        value["point"] = rendered["gaussian_panel"]["controls"][name]
        return value
    assert selector in {"menu.File", "menu.File.Close_image", "image.slice"}
    return rendered["host_controls"][selector]


def worker_events(value, source_id):
    assert type(value) is list
    assert [item["sequence"] for item in value] == list(range(1, len(value)+1))
    positive_gaussian_guards = set()
    for item in value:
        assert item["role"] == "native_worker" and item["operation"] == "gaussian_blur_imagej"
        assert item["source_id"] == source_id
        assert item["phase"] in {"worker_entered", "stage_progress"}
        assert type(item["stage"]) is str
        integer(item["completed"])
        integer(item["total"])
        address = item["resource_guard_address"]
        assert address is None or (type(address) is str and int(address, 16) > 0)
        if item["phase"] == "worker_entered":
            positive_gaussian_guards.discard(address)
        if item["phase"] == "stage_progress":
            assert item["completed"] <= item["total"]
            if item["stage"] == "Numeric arguments":
                # The actual resolved sigma pair is validated before the image kernel.
                assert item["total"] == 2 and item["completed"] in {0, 2}
            elif item["stage"] == "Immutable plane sample validation":
                # Dataset admission validates each produced 17x23 gray plane
                # inside this same worker's ExecutionScope after Gaussian work.
                # This exact storage stage is not Gaussian kernel evidence.
                assert item["total"] == SAMPLES and item["completed"] in {0, SAMPLES}
                assert address is not None and address in positive_gaussian_guards
            else:
                assert item["stage"].startswith("ImageJ Gaussian ")
                if item["stage"] == "ImageJ Gaussian source copy" and item["completed"] > 0:
                    positive_gaussian_guards.add(address)


def physical_frames(row, directory, adopted, source_id):
    frames = row["frames"]
    assert frames and len(frames) <= 1200
    assert [item["ordinal"] for item in frames] == list(range(1, len(frames)+1))
    sessions, prior_events = [], []
    for frame in frames:
        assert decode(member_path(directory, f"frame-{frame['ordinal']}.json").read_bytes()) == frame
        usage(frame["usage"])
        jobs(frame["actual_native_jobs"])
        worker_events(frame["worker_probe_events"], source_id)
        assert frame["worker_probe_events"][:len(prior_events)] == prior_events
        prior_events = frame["worker_probe_events"]
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
                atoms = event["keys"] if event["type"] == "keys" else [
                    {"name": event["name"], "down": event["down"]}]
                for atom in atoms:
                    assert set(atom) == {"name", "down"} and type(atom["down"]) is bool
                    assert (atom["name"] in actual["keys_down"]) is atom["down"]
            else:
                assert type(event["value"]) is str
    assert len({item["session_id"] for item in sessions}) == len({item["context_id"] for item in sessions}) == 1
    for before, after in zip(sessions, sessions[1:]):
        assert after["imgui_frame"] == before["imgui_frame"]+1 and after["imgui_time"] > before["imgui_time"]
    assert [item["ordinal"] for item in row["steps"]] == list(range(1, len(adopted["steps"])+1))
    assert [item["adopted_step"] for item in row["steps"]] == adopted["steps"]
    previous_last = 0
    for step in row["steps"]:
        first, last = step["first_frame_ordinal"], step["last_frame_ordinal"]
        integer(first)
        integer(last)
        assert previous_last <= first <= last <= len(frames)
        previous_last = last
        assert step["completed"] is True and step["before_frame_ordinal"] == first
        if step["adopted_step"]["op"] == "observe":
            assert first == last, "Observe must not inject input or a hidden acknowledgment"
        for frame in frames[first:last]:
            assert frame["step_ordinal"] == step["ordinal"]
    for observation in row["control_observations"]:
        selector = observation["selector"]
        prior = integer(observation["before_frame_ordinal"], positive=True)
        assert prior+3 <= len(frames)
        x, y = observation["position"]
        assert x == math.floor(x) and y == math.floor(y)
        if selector == "gaussian_panel.close_point":
            # The titlebar close is not the Gaussian panel's LastItemData.
            # Its point must come from the actual current titlebar geometry.
            assert observation["control"]["point"] == frames[prior-1]["rendered"]["gaussian_panel"]["close_point"]
            assert "id" not in observation["control"] and "rect" not in observation["control"]
            title = observation["control"]
            assert title["enabled"] is title["source_derived_geometry"] is True
            assert title["item_id_observed"] is title["rect_observed"] is False
            assert title["geometry_source"] == "GaussianPanel::draw actual standard titlebar close-button center"
            assert [x, y] == [math.floor(item) for item in title["point"]]
        else:
            control = observation["control"]
            integer(control["id"], positive=True)
            assert control["enabled"] is True
            assert type(control["active"]) is type(control["focused"]) is bool
            assert control == actual_control(frames[prior-1]["rendered"], selector)
            rect = control["rect"]
            assert len(rect) == 4 and all(type(item) in {int, float} and math.isfinite(item) for item in rect)
            assert rect[0] <= x < rect[2] and rect[1] <= y < rect[3]
            positioned = actual_control(frames[prior]["rendered"], selector)
            assert positioned["id"] == control["id"] and positioned["enabled"] is True
            assert positioned["rect"][0] <= x < positioned["rect"][2]
            assert positioned["rect"][1] <= y < positioned["rect"][3]
        assert [item["requested_events"] for item in frames[prior:prior+3]] == [
            [{"type": "position", "space": "screen", "point": [x, y]}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}],
        ]
    return frames


def resource_ledger(checkpoint):
    usage(checkpoint["usage"])
    observed = checkpoint["rendered"]["gaussian_jobs"]["resource_usage"]
    for key, native in (("running", "running"), ("queued", "queued"),
                        ("running_reserved_bytes", "running_bytes"),
                        ("retained_reserved_bytes", "retained_bytes"),
                        ("reserved_bytes", "reserved_bytes")):
        integer(observed[key])
        assert observed[key] == checkpoint["usage"][native]
    integer(observed["retained_preview_declared_bytes"])
    integer(observed["retained_apply_declared_bytes"])
    assert observed["ready_retention_reserves_budget"] is True
    assert observed["retained_resource_lease_required"] is False
    return observed


def preview_ready(checkpoint, before, directory, original, output, sigma, *, visible_preview=True):
    host = checkpoint["rendered"]["gaussian_jobs"]
    lane = host["preview"]
    generation = integer(lane["request_generation"], positive=True)
    assert host["requested_generation"] == host["installed_generation"] == host["active_installed_generation"] == generation
    assert lane["owns_job"] is True and lane["exit"] == "installed" and lane["error"] == ""
    assert lane["source_id"] == before["document_id"] and lane["published_document_id"] is None
    expected_record(lane["command_record"], before, sigma)
    assert lane["target"] == lane["command_record"]["target"]
    job = lane["job"]
    job_id = integer(job["id"], positive=True)
    actual = jobs(checkpoint["actual_native_jobs"])[job_id]
    assert actual["state"] == "READY" and job["state"] == "ready"
    assert actual["label"] == job["label"] == "Gaussian preview"
    assert actual["document_id"] == job["document_id"] == before["document_id"]
    assert actual["cancel_requested"] is job["cancel_requested"] is False
    assert actual["error"] == job["error"] == ""
    assert actual["resource_bytes"] == job["resource_bytes"] > 0
    result = checkpoint["preview_result"]
    assert result["job_id"] == job_id and result["request_generation"] == generation
    assert result["source_document_id"] == before["document_id"]
    assert result["command_record"] == lane["command_record"]
    z_count = before["shape_tcz_yxs"][2]
    assert [item["position"] for item in result["planes"]] == [[0, 0, z] for z in range(z_count)]
    for z, plane in enumerate(result["planes"]):
        assert raw(plane["raw"], directory) == (output if z == 0 else original)
    guards = storage(host["installed_preview_storage"], job_id=job_id, generation=generation,
                     source_id=before["document_id"], z_count=z_count)
    ledger = resource_ledger(checkpoint)
    assert checkpoint["usage"]["running"] == checkpoint["usage"]["queued"] == checkpoint["usage"]["running_bytes"] == 0
    assert ledger["retained_preview_declared_bytes"] == checkpoint["usage"]["retained_bytes"] == job["resource_bytes"]
    assert ledger["retained_apply_declared_bytes"] == 0
    assert [item["id"] for item in checkpoint["actual_native_jobs"] if item["state"] == "READY"] == [job_id]
    if visible_preview:
        cpu_readback(checkpoint, directory, output, preview=True, generation=generation)
    return job_id, generation, guards


def no_transient(checkpoint, directory, expected, *, closed=False, terminal=True):
    host = checkpoint["rendered"]["gaussian_jobs"]
    assert host["active_installed_generation"] is None
    assert host["installed_preview_storage"] is None
    assert host["preview"]["owns_job"] is False
    assert checkpoint["preview_result"] is None
    assert resource_ledger(checkpoint)["retained_preview_declared_bytes"] == 0
    if closed:
        ordinary = checkpoint["rendered"].get("ordinary_pixels")
        assert ordinary in (None, {})
    else:
        cpu_readback(checkpoint, directory, expected)
    if terminal:
        usage(checkpoint["usage"], empty=True)


def weak_guards(row, checkpoints, frames):
    observations = row["guard_observations"]
    assert [item["sequence"] for item in observations] == list(range(1, len(observations)+1))
    by_key = {}
    known = jobs(row["final_cleanup"]["actual_native_jobs"])
    worker_events(row["worker_probe_events"], checkpoints["baseline"]["native_state"]["documents"]["source"]["document_id"])
    context_guards = {item["resource_guard_address"] for item in row["worker_probe_events"]
                      if item["phase"] == "stage_progress" and item["completed"] > 0
                      and item["stage"].startswith("ImageJ Gaussian ")}
    for item in observations:
        job_id = integer(item["job_id"], positive=True)
        generation = integer(item["request_generation"], positive=True)
        assert job_id in known and known[job_id]["label"] == "Gaussian preview"
        address = item["guard_address"]
        assert type(address) is str and int(address, 16) > 0
        integer(item["frame_ordinal"], positive=True)
        integer(item["step_ordinal"])
        assert item["frame_ordinal"] <= len(frames)
        assert type(item["expired"]) is bool
        assert item["origin"] in {"actual READY tagged plane", "actual positive-work JobContext guard"}
        if item["origin"] == "actual positive-work JobContext guard":
            assert address in context_guards
        key = (job_id, generation, address)
        previous = by_key.setdefault(key, [])
        if previous:
            assert item["frame_ordinal"] >= previous[-1]["frame_ordinal"]
            assert not previous[-1]["expired"] or item["expired"], "A released real guard cannot resurrect in the same job/generation lineage"
        previous.append(item)
    assert observations, "A real Preview resource guard must be observed without owning it"
    for checkpoint in checkpoints.values():
        installed = checkpoint["rendered"]["gaussian_jobs"]["installed_preview_storage"]
        if installed is None:
            continue
        for address in installed["guard_addresses"]:
            key = (installed["job_id"], installed["request_generation"], address)
            assert key in by_key
            matching = [item for item in by_key[key] if item["checkpoint"] == checkpoint["name"]]
            assert matching and all(item["expired"] is False for item in matching)
    for trace in by_key.values():
        assert trace[0]["expired"] is False, "A real resource guard must first be independently observed alive"
        transitions = 0
        expired_before = False
        for item in trace:
            transitions += int(item["expired"] and not expired_before)
            expired_before = item["expired"]
            assert type(item["expiration_observation_count"]) is int
            assert item["expiration_observation_count"] == transitions
            assert item["count_scope"] == "observed live-to-expired transitions; not allocator/deleter invocation counts"
        assert trace[-1]["expired"] is True, "The last actual allocation guard still has a live owner after native cleanup"
        assert transitions == 1
    return by_key


def actual_barrier(checkpoint, name):
    barrier = checkpoint["barrier"]
    assert barrier["name"] == name
    assert barrier["armed"] is barrier["entered"] is True
    assert barrier["released"] is barrier["aborted"] is False
    job_id = integer(barrier["job_id"], positive=True)
    generation = integer(barrier["request_generation"], positive=True)
    guard = barrier["guard_address"]
    assert type(guard) is str and int(guard, 16) > 0
    actual = jobs(checkpoint["actual_native_jobs"])[job_id]
    assert actual["label"] == "Gaussian preview" and actual["state"] == "RUNNING"
    assert actual["resource_bytes"] > 0 and checkpoint["usage"]["running_bytes"] > 0
    events = [item for item in checkpoint["worker_probe_events"] if item["phase"] == "stage_progress"
              and item["stage"] == "ImageJ Gaussian source copy"
              and item["resource_guard_address"] == guard and 0 < item["completed"] <= item["total"]]
    assert events, "Barrier must be reached by genuine positive Gaussian kernel work"
    return job_id, generation, guard


def key_events(name):
    return [[{"type": "key", "name": name, "down": True}],
            [{"type": "key", "name": name, "down": False}], []]


def select_all_events():
    return [[{"type": "keys", "keys": [{"name": "Ctrl", "down": down},
                                         {"name": "A", "down": down}]}] for down in (True, False)]


def click_events(observation):
    return [[{"type": "position", "space": "screen", "point": observation["position"]}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}], []]


def physical_steps(row, frames, before):
    """Require every changed control to have its exact physical driver sequence."""
    by_step = {}
    for observation in row["control_observations"]:
        ordinal = integer(observation["step_ordinal"], positive=True)
        by_step.setdefault(ordinal, []).append(observation)
        assert observation["selected_live_document_id"] == before["document_id"]
        assert observation["attempted_disabled"] is False
    expected_api = []
    group = row["group_id"]
    if group in {"P02a.shared_ui.08.preview-generation-supersession", "P02a.shared_ui.13.source-lifetime-staleness"}:
        first = row["api_inputs"][0]
        assert first["step_ordinal"] == 0
        assert first["purpose"] == "declared real-worker barrier preparation; one worker prevents newer jobs overtaking held old worker"
        assert first["methods"] == ["JobScheduler.set_limits"]
        assert first["tested_changes_use_API_setters"] is False
        assert set(first["parameters"]) == {"workers", "memory_bytes", "capacity"}
        assert type(first["parameters"]["workers"]) is int and first["parameters"]["workers"] == 1
        integer(first["parameters"]["memory_bytes"], positive=True)
        integer(first["parameters"]["capacity"], positive=True)
        integer(first["previous_workers"], positive=True)
        expected_api.append(first)
    expected_api.append({"step_ordinal": 0,
                         "purpose": "explicit fresh UNBLURRED owned fixture/calibration preparation",
                         "role": "source", "methods": ["addNativeDocument", "setNativeDisplay", "mark_saved"],
                         "document_id": before["document_id"], "input_oracle": "u8-full-s2",
                         "calibration": CALIBRATION, "tested_changes_use_API_setters": False})
    for trace in row["steps"]:
        ordinal, adopted = trace["ordinal"], trace["adopted_step"]
        first, last = trace["first_frame_ordinal"], trace["last_frame_ordinal"]
        actual = [frame["requested_events"] for frame in frames[first:last]]
        observations = by_step.get(ordinal, [])
        op = adopted["op"]
        if op == "click":
            assert len(observations) == 1 and observations[0]["selector"] == adopted["selector"]
            assert observations[0]["before_frame_ordinal"] == first
            assert actual == click_events(observations[0])
        elif op == "replace_sigma":
            assert len(observations) == 1 and observations[0]["selector"] == "gaussian_panel.controls.sigma"
            assert observations[0]["before_frame_ordinal"] == first
            assert actual == click_events(observations[0])+select_all_events()+[
                [{"type": "text", "value": adopted["text"]}], []]
            focused = frames[first+3]["rendered"]["input_frame"]
            item_id = observations[0]["control"]["id"]
            assert focused["focus"]["active_id"] == focused["input_text"]["id"] == item_id
            panel = frames[last-1]["rendered"]["gaussian_panel"]
            assert panel["sigma_text"] == adopted["text"]
            assert panel["control_states"]["sigma"]["enabled"] is True
        elif op == "key_pulse":
            assert observations == [] and actual == key_events(adopted["name"])
            if adopted["name"] == "Enter":
                prior = frames[first-1]["rendered"]
                cancel = actual_control(prior, "gaussian_panel.controls.cancel")
                assert cancel["focused"] is True and prior["input_frame"]["focus"]["nav_id"] == cancel["id"]
        elif op == "focus_by_tabs":
            assert observations == [] and len(actual) % 3 == 0
            assert len(actual)//3 <= adopted["max_pulses"] == 16
            assert actual == key_events("Tab")*(len(actual)//3)
            render = frames[last-1]["rendered"]
            control = actual_control(render, adopted["selector"])
            assert control["focused"] is True and render["input_frame"]["focus"]["nav_id"] == control["id"]
        elif op == "set_actual_z_slider":
            assert len(observations) == 1 and observations[0]["selector"] == "image.slice"
            assert observations[0]["before_frame_ordinal"] == first+1
            assert actual == [[{"type": "key", "name": "Ctrl", "down": True}]]+click_events(
                observations[0])+[[{"type": "key", "name": "Ctrl", "down": False}]]+select_all_events()+[
                    [{"type": "text", "value": str(adopted["index"]+1)}]]+key_events("Enter")+[[]]
            item_id = observations[0]["control"]["id"]
            focused = frames[first+5]["rendered"]["input_frame"]
            assert focused["input_text"]["id"] == focused["focus"]["active_id"] == item_id
            after = frames[last-1]["rendered"]["host_controls"]["image.slice"]
            assert after["native_slice"] == adopted["index"]
            assert after["document_id"] == before["document_id"]
        elif op == "actual_close_captured_image":
            assert [item["selector"] for item in observations] == ["menu.File", "menu.File.Close_image"]
            assert [item["before_frame_ordinal"] for item in observations] == [first, first+5]
            assert actual == click_events(observations[0])+[[]]+click_events(observations[1])+[[]]
        elif op == "open_command":
            assert observations == [] and actual == [[]]
            expected_api.append({"step_ordinal": ordinal, "purpose": "adopted labelled_action_preparation opening only",
                                 "action": "show_operation", "parameters": {"operation": "gaussian_blur_imagej"}})
            panel = frames[last-1]["rendered"]["gaussian_panel"]
            assert panel["open"] is True and panel["source_id"] == before["document_id"]
            assert panel["preview"] is False and panel["sigma"] == 2 and panel["scaled_units"] is False
        elif op == "real_worker_barrier":
            assert observations == [] and actual == []
            expected_api.append({"step_ordinal": ordinal,
                                 "purpose": "adopted declared real-worker test preparation, not Gaussian control input",
                                 "name": adopted["name"], "mode": adopted["mode"],
                                 "methods": ["ScopedOperationJobProbe", "condition_variable"],
                                 "tested_changes_use_API_setters": False})
        elif op == "api_preparation":
            assert observations == [] and actual == [[], []]
            name = adopted["name"]
            if name == "external_pixels_commit":
                sample = fixture_bytes()[4*23+3]
                parameters = {"position": [0, 0, 0], "coordinate_xy": [3, 4],
                              "previous": sample, "replacement": (sample+1) % 256}
            elif name == "external_selection_commit":
                parameters = {"selection": {"kind": "rectangle", "points": [[5, 4], [16, 12]], "width": 1}}
            else:
                assert name == "external_calibration_commit"
                parameters = {"calibration": dict(CALIBRATION, spacing=[0.5, 1, 1], unit="um")}
            expected_api.append({"step_ordinal": ordinal,
                                 "purpose": "adopted explicit external staleness setup; no Gaussian UI execution credit",
                                 "name": name, "role": "source", "methods": ["Document.begin_edit", "Document.commit"],
                                 "document_id": before["document_id"], "parameters": parameters,
                                 "tested_changes_use_API_setters": False})
        else:
            assert op in {"observe", "await_native"} and observations == []
            assert all(events == [] for events in actual)
            if op == "await_native":
                assert 1 <= len(actual) <= adopted["max_pump_frames"] == 1200
                assert adopted["watchdog_seconds"] == 30
    expected_api.append({"step_ordinal": len(row["steps"]), "purpose": "bounded final/failure native job cleanup only",
                         "methods": ["Window.cancel", "action(wait_jobs,timeout_ms=5000)", "Window.poll"]})
    assert row["api_inputs"] == expected_api, "Unexpected programmatic preparation or substituted control setter"
    assert all(not frame["requested_events"] for frame in frames[:row["steps"][0]["first_frame_ordinal"]])
    assert all(not frame["requested_events"] for frame in frames[row["steps"][-1]["last_frame_ordinal"]:])


def checkpoint_index(row, directory, frames, adopted, source_id):
    expected = [("baseline", "baseline", 0, 0)]
    ready = 0
    for index, step in enumerate(adopted["steps"], 1):
        if step["op"] == "observe":
            name = "source_applied" if step["label"] == "applied" else step["label"]
            expected.append((name, step["label"], 1, index))
        elif step["op"] == "await_native" and step["condition"] in {
                "latest_preview_ready_and_rendered", "captured_plane_preview_rendered"}:
            ready += 1
            expected.append((f"preview_ready_{ready}", step["condition"], ready, index))
        elif step["op"] == "await_native" and step["condition"] in {"old_preview_barrier_reached", "preview_barrier_reached"}:
            expected.append(("worker_barrier_reached", "worker_barrier_reached", 0, index))
    expected += [("final", "final", 0, len(adopted["steps"])), ("cleanup", "cleanup", 0, len(adopted["steps"]))]
    assert [(item["name"], item["label"], item["occurrence"], item["step_ordinal"]) for item in row["checkpoints"]] == expected
    checkpoints = {}
    for item in row["checkpoints"]:
        assert item["name"] not in checkpoints
        ordinal = integer(item["frame_ordinal"], positive=True)
        assert ordinal <= len(frames)
        assert item["rendered"] == frames[ordinal-1]["rendered"]
        assert decode(member_path(directory, "checkpoint-"+item["name"]+".json").read_bytes()) == item
        usage(item["usage"])
        jobs(item["actual_native_jobs"])
        worker_events(item["worker_probe_events"], source_id)
        frame_events = frames[ordinal-1]["worker_probe_events"]
        assert item["worker_probe_events"][:len(frame_events)] == frame_events
        checkpoints[item["name"]] = item
        if item["step_ordinal"] and item["name"] not in {"final", "cleanup"}:
            trace = row["steps"][item["step_ordinal"]-1]
            assert trace["last_frame_ordinal"] == ordinal
            assert trace["checkpoint_name"] == (item["name"] if trace["adopted_step"]["op"] == "observe" else None)
    return checkpoints


def preview_panel(checkpoint, before, *, sigma=2, text=None, valid=True, enabled=True):
    panel = checkpoint["rendered"]["gaussian_panel"]
    assert panel["open"] is panel["visible"] is True and panel["source_id"] == before["document_id"]
    assert panel["draft_valid"] is valid and panel["scaled_available"] is panel["scaled_units"] is False
    assert panel["sigma"] == (sigma if valid else None)
    if text is not None:
        assert panel["sigma_text"] == text
    assert panel["preview"] is True
    assert panel["control_states"]["sigma"]["enabled"] is True
    assert panel["control_states"]["ok"]["enabled"] is enabled
    assert panel["control_states"]["preview"]["enabled"] is enabled
    assert type(panel["error"]) is str and bool(panel["error"]) is (not valid)
    assert panel["preview_error"] == ""
    return panel


def guard_expired_at(traces, checkpoint, job_id):
    matching = [trace for (job, _, _), trace in traces.items() if job == job_id]
    assert matching, "Required native guard never had an independent weak observation"
    for trace in matching:
        current = [item for item in trace if item["checkpoint"] == checkpoint["name"]]
        assert current and all(item["expired"] is True and item["expiration_observation_count"] == 1 for item in current)


def completed_apply(checkpoint, baseline, directory, original, output):
    before = baseline["native_state"]["documents"]["source"]
    after = checkpoint["native_state"]["documents"]["source"]
    document(after, directory, [output, original, original])
    assert after["document_id"] == before["document_id"] and after["data_identity"] != before["data_identity"]
    assert after["dirty"] is True and after["undo_steps"] == before["undo_steps"]+1 and after["redo_steps"] == 0
    assert after["revisions"]["pixels"] == before["revisions"]["pixels"]+1
    assert {key: value for key, value in after["revisions"].items() if key not in {"pixels", "display"}} == {
        key: value for key, value in before["revisions"].items() if key not in {"pixels", "display"}}
    for field in ("shape_tcz_yxs", "dtype", "calibration", "selection", "previous_selection", "rois",
                  "overlays", "spectrum_present", "metadata", "source_closed", "application_member", "desktop_member"):
        assert after[field] == before[field]
    assert after["display"]["crosshair"] == [11, 8, 2]
    assert {key: value for key, value in after["display"].items() if key not in {"crosshair", "revision"}} == {
        key: value for key, value in before["display"].items() if key not in {"crosshair", "revision"}}
    for actual, previous in zip(after["planes"][1:], before["planes"][1:]):
        assert {key: value for key, value in actual.items() if key != "raw"} == {
            key: value for key, value in previous.items() if key != "raw"}
    apply = checkpoint["rendered"]["gaussian_jobs"]["apply"]
    assert apply["exit"] == "published" and apply["owns_job"] is False and apply["error"] == ""
    assert apply["published_document_id"] == apply["source_id"] == before["document_id"]
    assert apply["request_generation"] is None
    expected_record(apply["command_record"], before, 2, apply=True)
    assert apply["target"] == apply["command_record"]["target"]
    actual = jobs(checkpoint["actual_native_jobs"])[apply["job"]["id"]]
    assert actual["state"] == "COMPLETED" and apply["job"]["state"] == "completed"
    assert actual["label"] == apply["job"]["label"] == "Gaussian Apply"
    assert actual["document_id"] == before["document_id"]
    assert actual["error"] == "" and actual["cancel_requested"] is False
    assert checkpoint["rendered"]["gaussian_jobs"]["apply_callback_count"] == 1
    no_transient(checkpoint, directory, original)
    return after, actual["id"]


def external_state(checkpoint, before, directory, original, suffix):
    """Independently predict the allowed external edit before checking late work."""
    value = checkpoint["native_state"]["documents"]["source"]
    closed = suffix == "actual-image-close"
    expected = bytearray(original)
    if suffix == "pixels":
        expected[4*23+3] = (expected[4*23+3]+1) % 256
    expected = bytes(expected)
    document(value, directory, [expected], closed=closed)
    if closed:
        same_state(value, before, directory, [original], closed=True)
        return value, expected
    for field in ("document_id", "shape_tcz_yxs", "dtype", "previous_selection", "rois", "overlays",
                  "spectrum_present", "metadata", "display", "source_closed", "application_member", "desktop_member"):
        assert value[field] == before[field]
    changed_revision = {"pixels": "pixels", "selection": "selection", "calibration": "metadata"}[suffix]
    assert value["revisions"] == {key: count+int(key == changed_revision) for key, count in before["revisions"].items()}
    if suffix == "selection":
        assert value["selection"] == {"kind": "rectangle", "points": [[5, 4], [16, 12]], "width": 1}
        assert value["calibration"] == before["calibration"]
        assert value["data_identity"] == before["data_identity"]
        assert value["history"] == before["history"] and value["dirty"] is before["dirty"]
        assert value["undo_steps"] == before["undo_steps"] and value["redo_steps"] == before["redo_steps"]
    else:
        assert value["selection"] == before["selection"]
        assert value["calibration"] == (dict(CALIBRATION, spacing=[0.5, 1, 1], unit="um")
                                        if suffix == "calibration" else before["calibration"])
        assert value["data_identity"] != before["data_identity"] and value["dirty"] is True
        assert value["undo_steps"] == before["undo_steps"]+1 and value["redo_steps"] == 0
    if suffix != "pixels":
        for actual, previous in zip(value["planes"], before["planes"]):
            assert {key: item for key, item in actual.items() if key != "raw"} == {
                key: item for key, item in previous.items() if key != "raw"}
    return value, expected


def assert_row(row, variant, context, directory, evidence):
    adopted = evidence["variants"][variant]
    group, suffix = variant.rsplit(".", 1)
    assert type(row["schema_version"]) is int and row["schema_version"] == 1
    assert row["group_id"] == group and row["case_id"] == variant and row["context"] == context
    assert row["status"] == "passed" and row["acceptance_claim"] is False
    assert row["contract_sha256"] == UI_CONTRACT_SHA and row["reference_capture_sha256"] == REFERENCE_SHA
    assert row["adopted_variant"] == adopted and adopted["runtime_observed"] is False
    assert all(row[field] is False for field in ("full61_verified", "full122_verified", "family_complete",
                                                "release_accepted", "nonfinite_image_workload_qualified"))
    assert row["comparison_scope"] == "Exact capture002 UInt8 bytes; no native primitive/scipy oracle"
    assert row["observation_scope"] == "Actual native planes and CPU upload-input/cache; no GPU readback"
    assert row["fixture_formula"] == (
        "owned asymmetric-analytic-v1: b=(37*x+61*y+13*x*y+19*(x%3)+23*(y%5))%256; "
        "UInt8=(b+(x==0?47:0)+(y==16?83:0))%256; each Z starts with identical unblurred source")
    assert decode(member_path(directory, "case.json").read_bytes()) == row
    original = evidence["input"]
    outputs = {sigma: evidence["oracles"][name]["output"] for sigma, name in (
        (0.5, "u8-full-s0p5"), (2, "u8-full-s2"), (12, "u8-full-s12"))}
    assert raw(row["fixture_input_raw"], directory) == original
    assert row["preview_oracles"] == {str(sigma): evidence["oracles"][name]["definition"] for sigma, name in (
        (2, "u8-full-s2"), (0.5, "u8-full-s0p5"), (12, "u8-full-s12"))}
    assert row["oracles"] == {"source": {"input": evidence["oracles"]["u8-full-s2"]["definition"],
        "output": evidence["oracles"][adopted["output_oracle"]]["definition"] if "output_oracle" in adopted else None}}
    assert row["checkpoints"][0]["name"] == "baseline"
    state0 = row["checkpoints"][0]["native_state"]
    before = state0["documents"]["source"]
    source_id = integer(before["document_id"], positive=True)
    navigation = group == "P02a.shared_ui.12.captured-navigation"
    stale = group == "P02a.shared_ui.13.source-lifetime-staleness"
    z_count = 3 if navigation else 1
    original_planes = [original]*z_count
    global_state(state0)
    seeded(before, directory, original_planes)
    assert state0["workflow"] == state0["results"] == [] and state0["recording"] is False
    frames = physical_frames(row, directory, adopted, source_id)
    physical_steps(row, frames, before)
    checkpoints = checkpoint_index(row, directory, frames, adopted, source_id)
    baseline = checkpoints["baseline"]
    assert baseline["actual_native_jobs"] == [] and baseline["preview_result"] is None
    usage(baseline["usage"], empty=True)
    initial_panel = baseline["rendered"]["gaussian_panel"]
    assert initial_panel["open"] is initial_panel["preview"] is False
    assert initial_panel["remembered"] == {"sigma": 2, "scaled_units": False}
    assert baseline["rendered"]["gaussian_jobs"]["apply_callback_count"] == 0
    cpu_readback(baseline, directory, original)
    guard_traces = weak_guards(row, checkpoints, frames)
    all_jobs = jobs(row["final_cleanup"]["actual_native_jobs"])
    assert all_jobs == jobs(checkpoints["cleanup"]["actual_native_jobs"])
    preview_ids = {job_id for job_id, job in all_jobs.items() if job["label"] == "Gaussian preview"}
    apply_ids = {job_id for job_id, job in all_jobs.items() if job["label"] == "Gaussian Apply"}
    assert set(all_jobs) == preview_ids | apply_ids
    assert len(apply_ids) == int(navigation)
    assert len(preview_ids) == (3 if group == "P02a.shared_ui.08.preview-generation-supersession" else
                                2 if group == "P02a.shared_ui.09.preview-invalid-recovery" else 1)
    assert {key[0] for key in guard_traces}.issubset(preview_ids)
    assert all(job["document_id"] == source_id and job["error"] == "" for job in all_jobs.values())
    assert all(all_jobs[job_id]["state"] == "CANCELED" and all_jobs[job_id]["cancel_requested"] is True
               for job_id in preview_ids)
    assert all(all_jobs[job_id]["state"] == "COMPLETED" and all_jobs[job_id]["cancel_requested"] is False
               for job_id in apply_ids)
    external_ordinal = next((trace["ordinal"] for trace in row["steps"] if trace["adopted_step"]["op"] in {
        "api_preparation", "actual_close_captured_image"}), None)
    for frame in frames:
        assert set(jobs(frame["actual_native_jobs"])).issubset(all_jobs)
        host = frame["rendered"]["gaussian_jobs"]
        if not stale or frame["step_ordinal"] < external_ordinal:
            assert host["request_error"] == "" and host["rejected_count"] == 0 and host["rejected_generations"] == []
        else:
            # Closing or changing the captured source can itself log an honest
            # rejected generation. It must never become a new admitted job.
            integer(host["rejected_count"])
            assert len(host["rejected_generations"]) == host["rejected_count"] <= 1
            if host["rejected_generations"]:
                assert host["rejected_generations"] == [checkpoints["worker_barrier_reached"]["barrier"]["request_generation"]]
                assert type(host["request_error"]) is str and host["request_error"]
        if not navigation:
            assert host["apply_callback_count"] == 0 and host["apply"]["owns_job"] is False
        if frame["preview_result"] is not None:
            expected_record(frame["preview_result"]["command_record"], before,
                            frame["preview_result"]["command_record"]["options"]["sigma"])
    # All observed committed source state remains unchanged until a declared
    # external edit or the one actual Stack No publication.
    for checkpoint in checkpoints.values():
        state = checkpoint["native_state"]
        closed = state["documents"]["source"]["source_closed"]
        global_state(state, closed=closed)
        assert state["workflow"] == state["results"] == [] and state["recording"] is False
        if (not stale or checkpoint["step_ordinal"] < external_ordinal) and checkpoint["name"] not in {
                "source_applied", "final", "cleanup"}:
            same_state(state["documents"]["source"], before, directory, original_planes, navigation=navigation)
        if not navigation:
            assert checkpoint["rendered"]["gaussian_panel"]["remembered"] == initial_panel["remembered"]

    ready_ids = []
    if not stale:
        first_ready = checkpoints["preview_ready_1"]
        sigma = 12 if group == "P02a.shared_ui.08.preview-generation-supersession" else (
            0.5 if group == "P02a.shared_ui.11.dialog-cancel-inputs" else 2)
        first_job, first_generation, _ = preview_ready(first_ready, before, directory, original, outputs[sigma], sigma)
        ready_ids.append(first_job)
        preview_panel(first_ready, before, sigma=sigma, text=str(sigma))
        assert first_ready["rendered"]["gaussian_panel"]["generation"] == first_generation
    if group == "P02a.shared_ui.07.preview-initial-and-on":
        off = checkpoints["preview_off"]
        assert off["actual_native_jobs"] == [] and off["rendered"]["gaussian_panel"]["preview"] is False
        no_transient(off, directory, original)
        observed = preview_ready(checkpoints["preview_ready"], before, directory, original, outputs[2], 2)
        assert observed[0:2] == (first_job, first_generation)
    elif group == "P02a.shared_ui.08.preview-generation-supersession":
        barrier_checkpoint = checkpoints["worker_barrier_reached"]
        old_job, old_generation, old_guard = actual_barrier(barrier_checkpoint, "old_preview_after_positive_work")
        assert barrier_checkpoint["rendered"]["gaussian_jobs"]["preview"]["job"]["id"] == old_job
        expected_record(barrier_checkpoint["rendered"]["gaussian_jobs"]["preview"]["command_record"], before, 2)
        middle, latest = checkpoints["middle_request"], checkpoints["latest_request"]
        observed = []
        for checkpoint, sigma in ((middle, 0.5), (latest, 12)):
            panel = preview_panel(checkpoint, before, sigma=sigma, text=str(sigma))
            lane = checkpoint["rendered"]["gaussian_jobs"]["preview"]
            generation = integer(panel["generation"], positive=True)
            assert lane["request_generation"] == checkpoint["rendered"]["gaussian_jobs"]["requested_generation"] == generation
            expected_record(lane["command_record"], before, sigma)
            actual = jobs(checkpoint["actual_native_jobs"])
            job_id = integer(lane["job"]["id"], positive=True)
            assert actual[job_id]["state"] == "QUEUED" and actual[job_id]["cancel_requested"] is False
            assert actual[old_job]["state"] == "RUNNING" and actual[old_job]["cancel_requested"] is True
            assert actual[old_job]["resource_bytes"] > 0
            assert checkpoint["barrier"]["entered"] is True and checkpoint["barrier"]["released"] is False
            assert checkpoint["barrier"]["guard_address"] == old_guard
            assert checkpoint["preview_result"] is None
            assert checkpoint["rendered"]["gaussian_jobs"]["installed_preview_storage"] is None
            cpu_readback(checkpoint, directory, original)
            observed.append((job_id, generation))
        middle_job, middle_generation = observed[0]
        newest_job, newest_generation = observed[1]
        assert old_generation < middle_generation < newest_generation == first_generation
        assert len({old_job, middle_job, newest_job}) == 3 and newest_job == first_job
        assert preview_ids == {old_job, middle_job, newest_job}
        assert jobs(latest["actual_native_jobs"])[middle_job]["state"] == "CANCELED"
        assert middle_job not in {key[0] for key in guard_traces}, "A queued canceled request cannot acquire native pixel resources"
        guard_expired_at(guard_traces, first_ready, old_job)
        observed_ready = preview_ready(checkpoints["latest_ready"], before, directory, original, outputs[12], 12)
        assert observed_ready[0:2] == (first_job, first_generation)
        assert first_ready["barrier"]["released"] is True
        for frame in frames:
            pixels = frame["rendered"].get("ordinary_pixels", {})
            if pixels.get("pixels_source") == "preview":
                assert pixels["preview_generation"] == newest_generation
            storage_value = frame["rendered"]["gaussian_jobs"]["installed_preview_storage"]
            if storage_value is not None:
                assert storage_value["job_id"] == newest_job and storage_value["request_generation"] == newest_generation
    elif group == "P02a.shared_ui.09.preview-invalid-recovery":
        invalid = checkpoints["invalid"]
        panel = preview_panel(invalid, before, text=adopted["invalid_text"], valid=False, enabled=False)
        assert panel["preview_requested"] is False
        no_transient(invalid, directory, original)
        assert set(jobs(invalid["actual_native_jobs"])) == {first_job}
        guard_expired_at(guard_traces, invalid, first_job)
        recovered = checkpoints["preview_ready_2"]
        recovered_job, recovered_generation, _ = preview_ready(recovered, before, directory, original, outputs[0.5], 0.5)
        assert recovered_job != first_job and recovered_generation > first_generation
        assert preview_ids == {first_job, recovered_job}
        preview_panel(checkpoints["recovered"], before, sigma=0.5, text="0.5")
        assert preview_ready(checkpoints["recovered"], before, directory, original, outputs[0.5], 0.5)[0:2] == (
            recovered_job, recovered_generation)
        ready_ids.append(recovered_job)
    elif group == "P02a.shared_ui.10.preview-off":
        off = checkpoints["preview_off"]
        assert off["rendered"]["gaussian_panel"]["preview"] is False
        no_transient(off, directory, original)
        guard_expired_at(guard_traces, off, first_job)
    elif group == "P02a.shared_ui.11.dialog-cancel-inputs":
        canceled, reopened = checkpoints["canceled"], checkpoints["reopened"]
        assert canceled["rendered"]["gaussian_panel"]["open"] is False
        assert canceled["rendered"]["gaussian_panel"]["exit_reason"] == "cancel"
        no_transient(canceled, directory, original)
        guard_expired_at(guard_traces, canceled, first_job)
        panel = reopened["rendered"]["gaussian_panel"]
        assert panel["open"] is True and panel["sigma_text"] == "2" and panel["sigma"] == 2
        assert panel["scaled_units"] is panel["preview"] is False and panel["draft_valid"] is True
        assert panel["remembered"] == {"sigma": 2, "scaled_units": False}
        assert set(jobs(reopened["actual_native_jobs"])) == {first_job}
        no_transient(reopened, directory, original)
        if suffix == "focused-cancel-enter":
            focused = checkpoints["cancel_focused"]["rendered"]
            cancel = actual_control(focused, "gaussian_panel.controls.cancel")
            assert cancel["focused"] is True and focused["input_frame"]["focus"]["nav_id"] == cancel["id"]
    elif navigation:
        away = checkpoints["navigated_away"]
        assert away["native_state"]["documents"]["source"]["display"]["crosshair"] == [11, 8, 2]
        assert away["rendered"]["host_controls"]["image.slice"]["native_slice"] == 2
        retained = preview_ready(away, before, directory, original, outputs[2], 2, visible_preview=False)
        assert retained[0:2] == (first_job, first_generation)
        cpu_readback(away, directory, original)
        returned = checkpoints["preview_ready_2"]
        assert returned["native_state"]["documents"]["source"]["display"]["crosshair"] == [11, 8, 0]
        assert preview_ready(returned, before, directory, original, outputs[2], 2)[0:2] == (first_job, first_generation)
        prompt = checkpoints["stack_prompt"]
        assert prompt["rendered"]["gaussian_panel"]["stack_prompt"] is prompt["rendered"]["gaussian_panel"]["open"] is True
        assert prompt["native_state"]["documents"]["source"]["display"]["crosshair"] == [11, 8, 2]
        assert set(jobs(prompt["actual_native_jobs"])) == {first_job}
        no_transient(prompt, directory, original)
        guard_expired_at(guard_traces, prompt, first_job)
        final_source, apply_job = completed_apply(checkpoints["source_applied"], baseline, directory, original, outputs[2])
        assert apply_ids == {apply_job}
        assert final_source["history"]["undo_steps"] == 1
    else:
        assert stale
        barrier_checkpoint = checkpoints["worker_barrier_reached"]
        old_job, old_generation, old_guard = actual_barrier(barrier_checkpoint, "preview_late_result")
        assert preview_ids == {old_job}
        expected_record(barrier_checkpoint["rendered"]["gaussian_jobs"]["preview"]["command_record"], before, 2)
        assert barrier_checkpoint["rendered"]["gaussian_jobs"]["preview"]["request_generation"] == old_generation
        post, late = checkpoints["post_external_stimulus"], checkpoints["late_completion"]
        final_source, final_pixels = external_state(post, before, directory, original, suffix)
        closed = suffix == "actual-image-close"
        expected_exit = "cancel" if closed else "source_changed"
        for checkpoint in (post, late):
            assert checkpoint["rendered"]["gaussian_panel"]["open"] is False
            assert checkpoint["rendered"]["gaussian_panel"]["exit_reason"] == expected_exit
            no_transient(checkpoint, directory, final_pixels, closed=closed, terminal=checkpoint is late)
            assert checkpoint["rendered"]["gaussian_jobs"]["apply_callback_count"] == 0
        assert post["barrier"]["released"] is False and post["barrier"]["guard_address"] == old_guard
        held = jobs(post["actual_native_jobs"])[old_job]
        assert held["state"] == "RUNNING" and held["cancel_requested"] is True and held["resource_bytes"] > 0
        assert post["usage"]["running_bytes"] > 0 and post["usage"]["retained_bytes"] == 0
        same_state(late["native_state"]["documents"]["source"], final_source, directory, [final_pixels], closed=closed)
        assert {key: value for key, value in late["native_state"].items() if key != "documents"} == {
            key: value for key, value in post["native_state"].items() if key != "documents"}
        assert jobs(late["actual_native_jobs"])[old_job]["state"] == "CANCELED"
        assert late["barrier"]["released"] is True and late["barrier"]["aborted"] is False
        guard_expired_at(guard_traces, late, old_job)
        assert all(frame["preview_result"] is None for frame in frames)
        assert all(frame["rendered"].get("ordinary_pixels", {}).get("pixels_source") != "preview" for frame in frames)

    final_checkpoint = checkpoints["final"]
    cleanup_checkpoint = checkpoints["cleanup"]
    closed = stale and suffix == "actual-image-close"
    expected_planes = [outputs[2], original, original] if navigation else [final_pixels] if stale else original_planes
    expected_state = final_source if navigation or stale else before
    for checkpoint in (final_checkpoint, cleanup_checkpoint):
        same_state(checkpoint["native_state"]["documents"]["source"], expected_state, directory, expected_planes, closed=closed)
        assert checkpoint["rendered"]["gaussian_panel"]["open"] is False
        no_transient(checkpoint, directory, expected_planes[2] if navigation else expected_planes[0], closed=closed)
    global_state(row["after"], closed=closed)
    same_state(row["after"]["documents"]["source"], expected_state, directory, expected_planes, closed=closed)
    assert row["after"]["workflow"] == row["after"]["results"] == [] and row["after"]["recording"] is False
    for job_id in ready_ids:
        guard_expired_at(guard_traces, final_checkpoint, job_id)
    assert set(row["original_after_raw"]) == {"source"}
    assert [item["position"] for item in row["original_after_raw"]["source"]] == [[0, 0, z] for z in range(z_count)]
    assert all(raw(item["raw"], directory) == original for item in row["original_after_raw"]["source"])
    worker_events(row["worker_probe_events"], source_id)
    assert row["worker_probe_events"][:len(frames[-1]["worker_probe_events"])] == frames[-1]["worker_probe_events"]
    assert all(event["source_id"] == source_id for event in row["worker_probe_events"])
    usage(row["final_cleanup"]["usage"], empty=True)
    assert row["final_cleanup"]["observer_retained_result_aliases"] == 0
    assert type(row["final_cleanup"]["observer_retained_result_aliases"]) is int
    assert row["final_cleanup"]["renderer_owns_native_jobs"] is False
    assert [item["label"] for item in row["lifecycle"]] == ["opened", "before_close", "closed"]
    opened, closing, ended = [item["observed"] for item in row["lifecycle"]]
    assert opened["open"] is closing["open"] is True and ended["open"] is False
    assert opened["session_id"] == closing["session_id"] == ended["session_id"] > 0
    assert opened["context_id"] == closing["context_id"] == frames[0]["rendered"]["session"]["context_id"] > 0
    assert ended["context_id"] == ended["current_context_id"] == 0
    assert row["final_cleanup"]["lifecycle"] == ended


@pytest.fixture(scope="module")
def preview_cpp_report():
    configured = os.environ.get("IMAGEJXX_PREVIEW_CPP_REPORT")
    if not configured:
        if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
            pytest.fail("Required installed standalone Preview report missing: IMAGEJXX_PREVIEW_CPP_REPORT")
        pytest.skip("Installed standalone Preview report is optional outside a required desktop gate")
    path = Path(configured).resolve(strict=True)
    report = decode(path.read_bytes())
    assert type(report["schema_version"]) is int and report["schema_version"] == 1
    assert report["context"] == "imgui_cpp" and report["status"] == "passed" and report["acceptance_claim"] is False
    assert report["contract_sha256"] == UI_CONTRACT_SHA and report["groups"] == list(GROUPS)
    assert all(report[field] is False for field in ("full61_verified", "full122_verified", "family_complete",
                                                   "release_accepted", "nonfinite_image_workload_qualified"))
    assert [row["case_id"] for row in report["cases"]] == VARIANTS
    return report, path.parent


def python_row(variant, directory):
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
        desktop = importlib.import_module("imagejxx._desktop")
    else:
        desktop = pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    assert Path(desktop.__file__).suffix in {".pyd", ".so"}
    editor = desktop.Editor(preferences_path=str(directory / "preferences.json"))
    return decode(editor._gaussian_preview_case(variant=variant, context="imgui_python", directory=str(directory)))


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("variant", VARIANTS)
def test_installed_gaussian_preview_ui(variant, context, request, tmp_path, preview_oracles):
    if context == "imgui_cpp":
        report, report_root = request.getfixturevalue("preview_cpp_report")
        row = next(item for item in report["cases"] if item["case_id"] == variant)
        directory = report_root / f"case{VARIANTS.index(variant)+1:02}"
    else:
        directory = tmp_path / "run"
        assert not directory.exists()
        row = python_row(variant, directory)
    assert_row(row, variant, context, directory, preview_oracles)
