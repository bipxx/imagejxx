"""Independent installed Gaussian stack and remembered-intent observations.

Source bytes use the owned analytic fixture. Numerical outputs use pinned raw
capture002 bytes, independently of producer PASS/expected fields. All current
adopted stack cases are UInt8; no Float32 or upstream dialog claim is made.
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
    "P02a.shared_ui.14.remembered-accepted-intent": ["accept-cancel-fresh-session"],
    "P02a.shared_ui.15.stack-no": ["pointer", "focused-no-enter"],
    "P02a.shared_ui.16.stack-yes": ["pointer", "default-yes-enter"],
    "P02a.shared_ui.17.stack-cancel-remembers": ["button", "escape", "titlebar"],
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
    pytest.fail("Cannot locate adopted Stack UI contract; configure its explicit path")



def configured_pin(path_name, hash_name, expected):
    configured, digest = os.environ.get(path_name), os.environ.get(hash_name)
    assert configured and digest, f"Required Stack evidence missing: {path_name}/{hash_name}"
    assert digest == expected
    path = Path(configured).resolve(strict=True)
    data = path.read_bytes()
    assert sha(data) == expected
    return data



@pytest.fixture(scope="module")
def stack_oracles():
    assert os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0") in {"0", "1"}
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") != "1" and not os.environ.get(
            "IMAGEJXX_GAUSSIAN_REFERENCE_JSONL"):
        pytest.skip("Groups14--17 installed UI is optional outside a required desktop gate")
    reference = configured_pin("IMAGEJXX_GAUSSIAN_REFERENCE_JSONL",
                               "IMAGEJXX_GAUSSIAN_REFERENCE_SHA256", REFERENCE_SHA)
    numeric = decode(configured_pin("IMAGEJXX_GAUSSIAN_CONTRACT_JSON",
                                    "IMAGEJXX_GAUSSIAN_CONTRACT_SHA256", NUMERIC_CONTRACT_SHA))
    assert type(numeric["schema_version"]) is int and numeric["schema_version"] == 1
    configured = os.environ.get("IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON")
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
        assert configured, "Required Stack gate must supply IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON"
    path = Path(configured) if configured else workspace() / (
        "build/plan-implementation/p02a-gaussian-command-001/shared-ui-case-contract-002.json")
    ui_raw = path.resolve(strict=True).read_bytes()
    assert sha(ui_raw) == UI_CONTRACT_SHA
    ui = decode(ui_raw)
    variants = [v for group in ui["cases"] if group["id"] in GROUPS for v in group["variants"]]
    assert [item["id"] for item in variants] == VARIANTS
    assert len(variants) == 8 and sum(len(item["steps"]) for item in variants) == 91
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



def seeded(value, directory, original_planes, role):
    document(value, directory, original_planes)
    assert value["calibration"] == CALIBRATION
    assert value["dirty"] is False and value["undo_steps"] == value["redo_steps"] == 0
    assert value["selection"] is value["previous_selection"] is None
    assert value["rois"] == value["overlays"] == [] and value["spectrum_present"] is False
    assert value["metadata"] == {"title": "Owned Gaussian stack "+role, "properties": {},
                                 "extensions": {}, "record_layout": {}}
    assert value["display"]["properties"]["display_range"] == [0, 255]
    assert value["display"]["t"] == value["display"]["c"] == 0
    assert value["display"]["crosshair"] == [11, 8, len(original_planes)//2]
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



def cpu_readback(checkpoint, directory, expected, *, preview=False, generation=None, role="source"):
    ordinary = checkpoint["rendered"]["ordinary_pixels"]
    assert ordinary["pixels_source"] == ("preview" if preview else "live")
    assert ordinary["contrast_source"] == "live"
    integer(ordinary["source_document_id"], positive=True)
    assert ordinary["source_document_id"] == checkpoint["native_state"]["documents"][role]["document_id"]
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
    assert selector in {"menu.Edit", "menu.Edit.Undo", "menu.Edit.Redo"}
    return rendered["host_controls"][selector]



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


def global_state(value, role):
    assert set(value["documents"]) in ({"source"}, {"source", "fresh_source"})
    assert role in value["documents"]
    active = value["documents"][role]
    integer(active["document_id"], positive=True)
    assert value["active_document_id"] == active["document_id"]
    assert value["application_document_ids"] == value["desktop_document_ids"] == [active["document_id"]]
    assert value["recording"] is False and value["workflow"] == value["results"] == []
    expected_hosts = {"primary"} | ({"fresh"} if "fresh_source" in value["documents"] else set())
    assert set(value["hosts"]) == expected_hosts
    for document_role, doc in value["documents"].items():
        host = "primary" if document_role == "source" else "fresh"
        assert doc["host"] == host
        assert doc["host_application_document_ids"] == doc["host_desktop_document_ids"] == [doc["document_id"]]
        assert doc["host_active_document_id"] == doc["document_id"]
        assert doc["source_closed"] is False and doc["application_member"] is doc["desktop_member"] is True
    for host in value["hosts"].values():
        usage(host["usage"])
        jobs(host["actual_native_jobs"])


def command_record(record, before, sigma, *, all_z=False):
    assert set(record) == {"schema_version", "operation", "options", "calibration", "effective_sigma_xy", "target"}
    assert type(record["schema_version"]) is int and record["schema_version"] == 1
    assert record["operation"] == "gaussian_blur_imagej"
    assert record["options"] == {"sigma": sigma, "scaled_units": False}
    assert type(record["options"]["sigma"]) in {int, float} and record["options"]["scaled_units"] is False
    assert record["calibration"] == before["calibration"] == CALIBRATION
    assert record["effective_sigma_xy"] == [sigma, sigma]
    assert all(type(item) in {int, float} and math.isfinite(item) for item in record["effective_sigma_xy"])
    z = before["display"]["crosshair"][2]
    z_range = [0, before["shape_tcz_yxs"][2], 1] if all_z else [z, z+1, 1]
    target = record["target"]
    assert target == {"schema_version": 1, "document_id": before["document_id"],
                      "source_revisions": [before["revisions"][key] for key in ("pixels", "metadata", "selection")],
                      "source_shape": before["shape_tcz_yxs"], "scope": "ranges",
                      "ranges": [[0, 1, 1], [0, 1, 1], z_range], "positions": [],
                      "roi_policy": "active_selection", "execution_mode": "planes", "destination": "in_place"}
    assert type(target["schema_version"]) is int
    for values in [target["source_revisions"], target["source_shape"], *target["ranges"]]:
        for item in values:
            integer(item)


def preferences_observation(checkpoint, directory):
    observed = checkpoint["preferences_file"]
    assert observed["exists"] is True
    path = Path(observed["path"]).resolve(strict=True)
    root = directory.resolve(strict=True)
    assert path.is_file() and path.parent in {root, root.parent}
    assert Path(checkpoint["rendered"]["preferences_path"]).resolve(strict=True) == path
    data = observed["bytes_utf8"].encode("utf-8")
    assert sha(data) == observed["sha256"] and decode(data) == observed["envelope"]
    envelope = observed["envelope"]
    assert envelope["format"] == "imagejxx-preferences" and type(envelope["version"]) is int and envelope["version"] == 1
    assert type(envelope["values"]) is type(envelope["extras"]) is dict
    for container in (envelope, envelope["values"], envelope["extras"]):
        assert not any(key.lower().startswith("gaussian") or key in {"sigma", "scaled_units"} for key in container)
    # The current saved file must still match the actual observations. The
    # envelope does not store accepted or canceled Gaussian dialog intent.
    assert path.read_bytes() == data
    return {"path": str(path), "bytes": data, "sha256": observed["sha256"], "envelope": envelope}


def physical_frames(row, directory, adopted):
    frames = row["frames"]
    assert frames and len(frames) <= 1200
    assert [item["ordinal"] for item in frames] == list(range(1, len(frames)+1))
    sessions = {}
    for frame in frames:
        assert frame["host"] in {"primary", "fresh"}
        assert decode(member_path(directory, f"frame-{frame['ordinal']}.json").read_bytes()) == frame
        usage(frame["usage"])
        jobs(frame["actual_native_jobs"])
        render = frame["rendered"]
        session = render["session"]
        assert session["open"] is True
        integer(session["session_id"], positive=True)
        integer(session["context_id"], positive=True)
        assert session["current_context_id"] == session["context_id"]
        sessions.setdefault(frame["host"], []).append(session)
        actual = render["input_frame"]
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
    for observations in sessions.values():
        assert len({s["session_id"] for s in observations}) == len({s["context_id"] for s in observations}) == 1
        for before, after in zip(observations, observations[1:]):
            assert after["imgui_frame"] == before["imgui_frame"]+1 and after["imgui_time"] > before["imgui_time"]
    assert len({items[0]["session_id"] for items in sessions.values()}) == len(sessions)
    # Allocator reuse of a destroyed ImGuiContext address is legal; session ID
    # and creator-owned close/open lifecycle, rather than address uniqueness,
    # prove the independent fresh renderer.
    assert [trace["ordinal"] for trace in row["steps"]] == list(range(1, len(adopted["steps"])+1))
    assert [trace["adopted_step"] for trace in row["steps"]] == adopted["steps"]
    previous = 0
    for trace in row["steps"]:
        first, last = trace["first_frame_ordinal"], trace["last_frame_ordinal"]
        integer(first)
        integer(last)
        assert previous <= first <= last <= len(frames)
        previous = last
        assert trace["before_frame_ordinal"] == first and trace["completed"] is True
        if trace["adopted_step"]["op"] == "observe":
            assert first == last
        assert all(frame["step_ordinal"] == trace["ordinal"] for frame in frames[first:last])
    for observation in row["control_observations"]:
        first = integer(observation["before_frame_ordinal"], positive=True)
        assert first+4 <= len(frames) and observation["attempted_disabled"] is False
        selector, control = observation["selector"], observation["control"]
        x, y = observation["position"]
        assert x == math.floor(x) and y == math.floor(y)
        if selector == "gaussian_panel.close_point":
            assert control["point"] == frames[first-1]["rendered"]["gaussian_panel"]["close_point"]
            assert control["enabled"] is control["source_derived_geometry"] is True
            assert control["item_id_observed"] is control["rect_observed"] is False
            assert "id" not in control and "rect" not in control
            assert control["geometry_source"] == "GaussianPanel::draw actual standard titlebar close-button center"
            assert [x, y] == [math.floor(value) for value in control["point"]]
        else:
            integer(control["id"], positive=True)
            assert control == actual_control(frames[first-1]["rendered"], selector)
            assert control["enabled"] is True and type(control["active"]) is type(control["focused"]) is bool
            rect = control["rect"]
            assert len(rect) == 4 and rect[0] <= x < rect[2] and rect[1] <= y < rect[3]
            positioned = actual_control(frames[first]["rendered"], selector)
            assert positioned["id"] == control["id"] and positioned["enabled"] is True
            assert positioned["rect"][0] <= x < positioned["rect"][2] and positioned["rect"][1] <= y < positioned["rect"][3]
        assert [frame["requested_events"] for frame in frames[first:first+4]] == click_events(observation)
    return frames, sessions


def checkpoint_index(row, directory, frames, adopted):
    expected = [("baseline", "baseline", 0, 0)]
    ready = 0
    for ordinal, step in enumerate(adopted["steps"], 1):
        if step["op"] == "observe":
            name = "source_applied" if step["label"] == "applied" else step["label"]
            expected.append((name, step["label"], 1, ordinal))
        elif step["op"] == "fresh_independent_host_session":
            expected += [("primary_final", "primary_final", 0, ordinal), ("fresh_baseline", "fresh_baseline", 0, ordinal)]
        elif step["op"] == "await_native" and step["condition"] == "latest_preview_ready_and_rendered":
            ready += 1
            expected.append((f"preview_ready_{ready}", step["condition"], ready, ordinal))
    expected += [("final", "final", 0, len(adopted["steps"])), ("cleanup", "cleanup", 0, len(adopted["steps"]))]
    assert [(item["name"], item["label"], item["occurrence"], item["step_ordinal"]) for item in row["checkpoints"]] == expected
    result = {}
    for point in row["checkpoints"]:
        assert point["name"] not in result
        first = integer(point["frame_ordinal"], positive=True)
        assert first <= len(frames) and point["rendered"] == frames[first-1]["rendered"]
        assert decode(member_path(directory, "checkpoint-"+point["name"]+".json").read_bytes()) == point
        assert point["host"] == frames[first-1]["host"]
        usage(point["usage"])
        jobs(point["actual_native_jobs"])
        if point["step_ordinal"] and point["name"] not in {"final", "cleanup", "primary_final"}:
            trace = row["steps"][point["step_ordinal"]-1]
            assert trace["last_frame_ordinal"] == first
            assert trace["checkpoint_name"] == (point["name"] if trace["adopted_step"]["op"] in {
                "observe", "fresh_independent_host_session"} else None)
        result[point["name"]] = point
    return result


def physical_steps(row, frames, source_ids):
    observations = {}
    for item in row["control_observations"]:
        observations.setdefault(item["step_ordinal"], []).append(item)
        host = frames[item["before_frame_ordinal"]-1]["host"]
        assert item["selected_live_document_id"] == source_ids[host]
    for trace in row["steps"]:
        step, ordinal = trace["adopted_step"], trace["ordinal"]
        first, last = trace["first_frame_ordinal"], trace["last_frame_ordinal"]
        actual = [item["requested_events"] for item in frames[first:last]]
        controls = observations.get(ordinal, [])
        op = step["op"]
        if op == "click":
            assert len(controls) == 1 and controls[0]["selector"] == step["selector"]
            assert controls[0]["before_frame_ordinal"] == first
            assert actual[:4] == click_events(controls[0])
            if step["selector"] == "gaussian_panel.controls.ok" and row["group_id"] != "P02a.shared_ui.14.remembered-accepted-intent":
                # Default Yes focus is deferred by native ImGui navigation.
                # Only passive frames inside the actual OK macro may settle it;
                # literal observe never delivers an input or resets a NavId.
                assert len(actual)-4 <= 6 and all(value == [] for value in actual[4:])
                assert all(frame["label"] == "stack_prompt_default_focus_settled" for frame in frames[first+4:last])
            else:
                assert len(actual) == 4
        elif op == "replace_sigma":
            assert len(controls) == 1 and controls[0]["selector"] == "gaussian_panel.controls.sigma"
            assert controls[0]["before_frame_ordinal"] == first
            assert actual == click_events(controls[0])+select_all_events()+[[{"type": "text", "value": step["text"]}], []]
            input_state = frames[first+3]["rendered"]["input_frame"]
            assert input_state["focus"]["active_id"] == input_state["input_text"]["id"] == controls[0]["control"]["id"]
            panel = frames[last-1]["rendered"]["gaussian_panel"]
            assert panel["sigma_text"] == step["text"] and panel["control_states"]["sigma"]["enabled"] is True
        elif op == "key_pulse":
            assert controls == [] and actual == key_events(step["name"])
            if step["name"] == "Enter":
                selector = "gaussian_panel.controls.stack_no" if row["group_id"] == "P02a.shared_ui.15.stack-no" else "gaussian_panel.controls.stack_yes"
                previous = frames[first-1]["rendered"]
                choice = actual_control(previous, selector)
                assert choice["focused"] is True and previous["input_frame"]["focus"]["nav_id"] == choice["id"]
        elif op == "focus_by_tabs":
            assert controls == [] and len(actual) % 3 == 0 and len(actual)//3 <= step["max_pulses"] == 16
            assert actual == key_events("Tab")*(len(actual)//3)
            render = frames[last-1]["rendered"]
            choice = actual_control(render, step["selector"])
            assert choice["focused"] is True and render["input_frame"]["focus"]["nav_id"] == choice["id"]
        elif op in {"actual_undo", "actual_redo"}:
            item = "Undo" if op == "actual_undo" else "Redo"
            assert [value["selector"] for value in controls] == ["menu.Edit", "menu.Edit."+item]
            assert [value["before_frame_ordinal"] for value in controls] == [first, first+5]
            assert actual == click_events(controls[0])+[[]]+click_events(controls[1])+[[]]
        else:
            assert controls == [] and op in {"open_command", "observe", "await_native", "fresh_independent_host_session"}
            assert all(value == [] for value in actual)
            if op == "open_command":
                assert actual == [[]]
                panel = frames[last-1]["rendered"]["gaussian_panel"]
                assert panel["open"] is True and panel["source_id"] == source_ids[frames[last-1]["host"]]
                assert panel["draft_valid"] is True and panel["preview"] is False and panel["scaled_units"] is False
            elif op == "await_native":
                assert 1 <= len(actual) <= step["max_pump_frames"] == 1200 and step["watchdog_seconds"] == 30
            elif op == "fresh_independent_host_session":
                assert actual == [[], [], []]
                assert [frame["host"] for frame in frames[first:last]] == ["primary", "fresh", "fresh"]
    assert all(frame["requested_events"] == [] for frame in frames[:row["steps"][0]["first_frame_ordinal"]])
    assert all(frame["requested_events"] == [] for frame in frames[row["steps"][-1]["last_frame_ordinal"]:])


def preserved(value, before):
    for field in ("document_id", "shape_tcz_yxs", "dtype", "calibration", "selection", "previous_selection", "rois",
                  "overlays", "spectrum_present", "metadata", "display", "source_closed", "application_member",
                  "desktop_member", "host", "host_application_document_ids", "host_desktop_document_ids", "host_active_document_id"):
        assert value[field] == before[field]
    for field in ("metadata", "selection", "display"):
        assert value["revisions"][field] == before["revisions"][field]
    assert value["history"]["step_limit"] == before["history"]["step_limit"]
    assert value["history"]["pixel_budget"] == before["history"]["pixel_budget"]
    assert value["history"]["pixels"]["current_bytes"] == before["history"]["pixels"]["current_bytes"]


def changed(checkpoint, before, directory, planes, sigma, *, all_z=False):
    value = checkpoint["native_state"]["documents"]["source"]
    document(value, directory, planes)
    preserved(value, before)
    assert value["data_identity"] != before["data_identity"] and value["dirty"] is True
    assert value["undo_steps"] == before["undo_steps"]+1 and value["redo_steps"] == 0
    assert value["revisions"]["pixels"] == before["revisions"]["pixels"]+1
    targeted = set(range(len(planes))) if all_z else {before["display"]["crosshair"][2]}
    for z, (actual, old) in enumerate(zip(value["planes"], before["planes"])):
        if z not in targeted:
            assert {key: item for key, item in actual.items() if key != "raw"} == {
                key: item for key, item in old.items() if key != "raw"}
    host = checkpoint["rendered"]["gaussian_jobs"]
    lane = host["apply"]
    assert lane["exit"] == "published" and lane["owns_job"] is False and lane["error"] == ""
    assert lane["source_id"] == lane["published_document_id"] == before["document_id"]
    assert lane["request_generation"] is None
    command_record(lane["command_record"], before, sigma, all_z=all_z)
    assert lane["target"] == lane["command_record"]["target"]
    actual = jobs(checkpoint["actual_native_jobs"])[lane["job"]["id"]]
    assert actual["label"] == lane["job"]["label"] == "Gaussian Apply"
    assert actual["state"] == "COMPLETED" and lane["job"]["state"] == "completed"
    assert actual["document_id"] == before["document_id"]
    assert actual["cancel_requested"] is False and actual["error"] == ""
    assert host["apply_callback_count"] == 1
    cpu_readback(checkpoint, directory, planes[before["display"]["crosshair"][2]])
    return value, actual["id"]


def history_step(checkpoint, before, applied, directory, planes, *, redo):
    value = checkpoint["native_state"]["documents"]["source"]
    document(value, directory, planes)
    preserved(value, before)
    assert value["undo_steps"] == int(redo) and value["redo_steps"] == int(not redo)
    assert value["dirty"] is redo and value["data_identity"] == (applied if redo else before)["data_identity"]
    assert value["revisions"]["pixels"] == before["revisions"]["pixels"]+(3 if redo else 2)
    observed = applied if redo else before
    for plane, original in zip(value["planes"], observed["planes"]):
        assert {key: item for key, item in plane.items() if key != "raw"} == {
            key: item for key, item in original.items() if key != "raw"}
    assert checkpoint["rendered"]["gaussian_jobs"]["apply_callback_count"] == 1
    cpu_readback(checkpoint, directory, planes[before["display"]["crosshair"][2]])
    return value


def no_transient(checkpoint, directory, expected, *, role="source"):
    host = checkpoint["rendered"]["gaussian_jobs"]
    assert host["active_installed_generation"] is None and host["installed_preview_storage"] is None
    assert host["preview"]["owns_job"] is False and checkpoint["preview_result"] is None
    assert resource_ledger(checkpoint)["retained_preview_declared_bytes"] == 0
    usage(checkpoint["usage"], empty=True)
    cpu_readback(checkpoint, directory, expected, role=role)


def preview_ready(checkpoint, before, directory, original, output):
    host = checkpoint["rendered"]["gaussian_jobs"]
    lane = host["preview"]
    generation = integer(lane["request_generation"], positive=True)
    assert checkpoint["rendered"]["gaussian_panel"]["generation"] == generation
    assert host["requested_generation"] == host["installed_generation"] == host["active_installed_generation"] == generation
    assert lane["owns_job"] is True and lane["exit"] == "installed" and lane["error"] == ""
    assert lane["source_id"] == before["document_id"] and lane["published_document_id"] is None
    command_record(lane["command_record"], before, 0.5)
    assert lane["target"] == lane["command_record"]["target"]
    job_id = integer(lane["job"]["id"], positive=True)
    actual = jobs(checkpoint["actual_native_jobs"])[job_id]
    assert actual["state"] == "READY" and lane["job"]["state"] == "ready"
    assert actual["label"] == lane["job"]["label"] == "Gaussian preview"
    assert actual["document_id"] == lane["job"]["document_id"] == before["document_id"]
    assert actual["cancel_requested"] is lane["job"]["cancel_requested"] is False
    assert actual["error"] == lane["job"]["error"] == "" and actual["resource_bytes"] == lane["job"]["resource_bytes"] > 0
    result = checkpoint["preview_result"]
    assert result["job_id"] == job_id and result["request_generation"] == generation
    assert result["source_document_id"] == before["document_id"] and result["command_record"] == lane["command_record"]
    assert [plane["position"] for plane in result["planes"]] == [[0, 0, z] for z in range(3)]
    for z, plane in enumerate(result["planes"]):
        assert raw(plane["raw"], directory) == (output if z == 1 else original)
    guards = storage(host["installed_preview_storage"], job_id=job_id, generation=generation, source_id=before["document_id"], z_count=3)
    ledger = resource_ledger(checkpoint)
    assert checkpoint["usage"]["running"] == checkpoint["usage"]["queued"] == checkpoint["usage"]["running_bytes"] == 0
    assert ledger["retained_preview_declared_bytes"] == checkpoint["usage"]["retained_bytes"] == actual["resource_bytes"]
    assert ledger["retained_apply_declared_bytes"] == 0
    assert set(jobs(checkpoint["actual_native_jobs"])) == {job_id}
    cpu_readback(checkpoint, directory, output, preview=True, generation=generation)
    return job_id, generation, guards


def weak_guards(row, frames, checkpoints, known_jobs):
    observations = row["guard_observations"]
    assert [item["sequence"] for item in observations] == list(range(1, len(observations)+1))
    traces = {}
    for item in observations:
        assert item["origin"] == "actual READY tagged plane"
        assert item["count_scope"] == "observed live-to-expired transitions; not allocator/deleter invocation counts"
        job_id = integer(item["job_id"], positive=True)
        assert known_jobs[job_id]["label"] == "Gaussian preview"
        generation = integer(item["request_generation"], positive=True)
        address = item["guard_address"]
        assert type(address) is str and int(address, 16) > 0
        integer(item["frame_ordinal"], positive=True)
        integer(item["step_ordinal"])
        assert item["frame_ordinal"] <= len(frames) and type(item["expired"]) is bool
        trace = traces.setdefault((job_id, generation, address), [])
        if trace:
            assert item["frame_ordinal"] >= trace[-1]["frame_ordinal"]
            assert not trace[-1]["expired"] or item["expired"]
        trace.append(item)
    for trace in traces.values():
        assert trace[0]["expired"] is False and trace[-1]["expired"] is True
        previous, transitions = False, 0
        for item in trace:
            transitions += int(item["expired"] and not previous)
            previous = item["expired"]
            assert type(item["expiration_observation_count"]) is int and item["expiration_observation_count"] == transitions
        assert transitions == 1
    for checkpoint in checkpoints.values():
        installed = checkpoint["rendered"]["gaussian_jobs"]["installed_preview_storage"]
        if installed is None:
            continue
        for address in installed["guard_addresses"]:
            key = (installed["job_id"], installed["request_generation"], address)
            assert key in traces
            matching = [item for item in traces[key] if item["checkpoint"] == checkpoint["name"]]
            assert matching and all(item["expired"] is False for item in matching)
    return traces


def api_inputs(row, frames, checkpoints, source_ids):
    def fixture(role, ordinal):
        host = "primary" if role == "source" else "fresh"
        return {"step_ordinal": ordinal, "purpose": "explicit fresh UNBLURRED owned fixture/calibration preparation",
                "role": role, "methods": ["addNativeDocument", "setNativeDisplay", "mark_saved"],
                "document_id": source_ids[host], "input_oracle": "u8-full-s2", "calibration": CALIBRATION,
                "tested_changes_use_API_setters": False}
    def cleanup(ordinal):
        return {"step_ordinal": ordinal, "purpose": "bounded final/failure native job cleanup only",
                "methods": ["Window.cancel", "action(wait_jobs,timeout_ms=5000)", "Window.poll"]}
    expected = [fixture("source", 0)]
    if "fresh_baseline" in checkpoints:
        observed = checkpoints["baseline"]["preferences_file"]
        expected.append({"step_ordinal": 0,
                         "purpose": "unchanged defaults persistence solely for same-file fresh-session fixture proof",
                         "method": "Window.updateSettings", "values": {}, "persist": True,
                         "preferences_path": observed["path"], "tested_changes_use_API_setters": False})
    for trace in row["steps"]:
        op, ordinal = trace["adopted_step"]["op"], trace["ordinal"]
        if op == "open_command":
            expected.append({"step_ordinal": ordinal, "purpose": "adopted labelled_action_preparation opening only",
                             "action": "show_operation", "parameters": {"operation": "gaussian_blur_imagej"}})
        elif op == "fresh_independent_host_session":
            first, last = trace["first_frame_ordinal"], trace["last_frame_ordinal"]
            primary = frames[first]["rendered"]["session"]
            fresh = frames[last-1]["rendered"]["session"]
            expected += [cleanup(ordinal), fixture("fresh_source", ordinal), {
                "step_ordinal": ordinal, "purpose": "adopted fresh independent host/session preparation; no remembered-state reset setter",
                "methods": ["Window(same_observed_preferences_path)", "HeadlessInputSession"],
                "preferences_path": checkpoints["baseline"]["preferences_file"]["path"],
                "previous_session_id": primary["session_id"], "new_session_id": fresh["session_id"],
                "role": "fresh_source", "tested_changes_use_API_setters": False}]
    expected.append(cleanup(len(row["steps"])))
    assert row["api_inputs"] == expected, "Undeclared reset/setter/publication must not substitute for adopted physical input"


def assert_row(row, variant, context, directory, evidence):
    adopted = evidence["variants"][variant]
    group, suffix = variant.rsplit(".", 1)
    remembered = group == "P02a.shared_ui.14.remembered-accepted-intent"
    cancel = group == "P02a.shared_ui.17.stack-cancel-remembers"
    all_z = group == "P02a.shared_ui.16.stack-yes"
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
        "UInt8=(b+(x==0?47:0)+(y==16?83:0))%256; every declared Z begins with the same unblurred input")
    assert decode(member_path(directory, "case.json").read_bytes()) == row
    original = evidence["input"]
    assert raw(row["fixture_input_raw"], directory) == original
    frames, sessions = physical_frames(row, directory, adopted)
    checkpoints = checkpoint_index(row, directory, frames, adopted)
    baseline = checkpoints["baseline"]
    before = baseline["native_state"]["documents"]["source"]
    original_planes = [original]*(1 if remembered else 3)
    seeded(before, directory, original_planes, "source")
    global_state(baseline["native_state"], "source")
    assert baseline["actual_native_jobs"] == [] and baseline["preview_result"] is None
    usage(baseline["usage"], empty=True)
    initial_panel = baseline["rendered"]["gaussian_panel"]
    assert initial_panel["remembered"] == {"sigma": 2, "scaled_units": False}
    assert initial_panel["open"] is initial_panel["preview"] is False
    no_transient(baseline, directory, original)
    assert baseline["rendered"]["gaussian_jobs"]["apply_callback_count"] == 0
    source_ids = {"primary": before["document_id"]}
    fresh_before = None
    if remembered:
        fresh = checkpoints["fresh_baseline"]
        fresh_before = fresh["native_state"]["documents"]["fresh_source"]
        seeded(fresh_before, directory, [original], "fresh_source")
        source_ids["fresh"] = fresh_before["document_id"]
        assert source_ids["primary"] != source_ids["fresh"]
        assert fresh["actual_native_jobs"] == [] and fresh["rendered"]["gaussian_jobs"]["apply_callback_count"] == 0
        assert fresh["rendered"]["gaussian_panel"]["remembered"] == {"sigma": 2, "scaled_units": False}
        assert fresh["rendered"]["gaussian_panel"]["open"] is fresh["rendered"]["gaussian_panel"]["preview"] is False
        no_transient(fresh, directory, original, role="fresh_source")
    assert set(sessions) == set(source_ids)
    physical_steps(row, frames, source_ids)
    api_inputs(row, frames, checkpoints, source_ids)
    for checkpoint in checkpoints.values():
        role = "fresh_source" if checkpoint["host"] == "fresh" else "source"
        global_state(checkpoint["native_state"], role)
        assert checkpoint["settings"] == baseline["settings"]
        if remembered:
            assert preferences_observation(checkpoint, directory) == preferences_observation(baseline, directory)
            assert checkpoint["preferences_file"]["envelope"] == checkpoint["settings"]
        else:
            assert checkpoint["preferences_file"] == baseline["preferences_file"]
            assert checkpoint["preferences_file"]["exists"] is False
            assert all(checkpoint["preferences_file"][key] is None for key in ("bytes_utf8", "envelope", "sha256"))
    expected_oracles = {"source": {"input": evidence["oracles"]["u8-full-s2"]["definition"],
        "output": None if cancel else evidence["oracles"]["u8-full-s0p5" if remembered else "u8-full-s2"]["definition"]}}
    if remembered:
        expected_oracles["fresh_source"] = {"input": evidence["oracles"]["u8-full-s2"]["definition"], "output": None}
    assert row["oracles"] == expected_oracles
    host_final = row["final_cleanup"]["hosts"]
    assert set(host_final) == set(source_ids)
    primary_jobs = jobs(host_final["primary"]["actual_native_jobs"])
    assert len(primary_jobs) == 1
    only_job = next(iter(primary_jobs.values()))
    assert only_job["document_id"] == before["document_id"] and only_job["error"] == ""
    assert only_job["label"] == ("Gaussian preview" if cancel else "Gaussian Apply")
    assert only_job["state"] == ("CANCELED" if cancel else "COMPLETED")
    assert only_job["cancel_requested"] is cancel
    for host, values in host_final.items():
        usage(values["usage"], empty=True)
        if host == "fresh":
            assert values["actual_native_jobs"] == []
    for frame in frames:
        known = primary_jobs if frame["host"] == "primary" else {}
        assert set(jobs(frame["actual_native_jobs"])).issubset(known)
        host = frame["rendered"]["gaussian_jobs"]
        assert host["request_error"] == "" and host["rejected_count"] == 0 and host["rejected_generations"] == []
        if cancel or frame["host"] == "fresh":
            assert host["apply_callback_count"] == 0 and host["apply"]["owns_job"] is False
        if not cancel:
            assert host["preview"]["owns_job"] is False and frame["preview_result"] is None
    traces = weak_guards(row, frames, checkpoints, primary_jobs)

    if remembered:
        output = evidence["oracles"]["u8-full-s0p5"]["output"]
        applied, job_id = changed(checkpoints["source_applied"], before, directory, [output], 0.5)
        assert job_id == only_job["id"] and traces == {}
        for name in ("remembered_0p5", "still_0p5", "primary_final"):
            point = checkpoints[name]
            same_state(point["native_state"]["documents"]["source"], applied, directory, [output])
            no_transient(point, directory, output)
            assert point["rendered"]["gaussian_panel"]["remembered"] == {"sigma": 0.5, "scaled_units": False}
            if name != "primary_final":
                panel = point["rendered"]["gaussian_panel"]
                assert panel["open"] is True and panel["sigma"] == 0.5 and panel["sigma_text"] == "0.5"
                assert panel["scaled_units"] is panel["preview"] is False
            else:
                assert point["rendered"]["gaussian_panel"]["open"] is False
        for point in checkpoints.values():
            if point["host"] == "fresh":
                same_state(point["native_state"]["documents"]["source"], applied, directory, [output])
                same_state(point["native_state"]["documents"]["fresh_source"], fresh_before, directory, [original])
        defaults = checkpoints["fresh_defaults"]["rendered"]["gaussian_panel"]
        assert defaults["open"] is True and defaults["sigma"] == 2 and defaults["sigma_text"] == "2"
        assert defaults["scaled_units"] is defaults["preview"] is False
        assert defaults["remembered"] == {"sigma": 2, "scaled_units": False}
        expected_final = {"source": (applied, [output]), "fresh_source": (fresh_before, [original])}
    else:
        prompt = checkpoints["stack_prompt"]
        same_state(prompt["native_state"]["documents"]["source"], before, directory, original_planes)
        panel = prompt["rendered"]["gaussian_panel"]
        assert panel["open"] is panel["stack_prompt"] is True and panel["preview"] is False
        assert panel["remembered"] == {"sigma": 0.5 if cancel else 2, "scaled_units": False}
        yes = actual_control(prompt["rendered"], "gaussian_panel.controls.stack_yes")
        assert yes["focused"] is True and prompt["rendered"]["input_frame"]["focus"]["nav_id"] == yes["id"]
        assert prompt["rendered"]["gaussian_jobs"]["apply_callback_count"] == 0
        assert prompt["rendered"]["gaussian_jobs"]["apply"]["owns_job"] is False
        no_transient(prompt, directory, original)
        if cancel:
            output = evidence["oracles"]["u8-full-s0p5"]["output"]
            preview_job, generation, addresses = preview_ready(checkpoints["preview_ready_1"], before, directory, original, output)
            assert preview_job == only_job["id"] and traces
            assert {(job_id, gen, address) for job_id, gen, address in traces} == {
                (preview_job, generation, address) for address in addresses}
            for trace in traces.values():
                at_prompt = [item for item in trace if item["checkpoint"] == "stack_prompt"]
                assert at_prompt and all(item["expired"] is True and item["expiration_observation_count"] == 1 for item in at_prompt)
            for point in checkpoints.values():
                same_state(point["native_state"]["documents"]["source"], before, directory, original_planes)
            canceled = checkpoints["canceled"]
            assert canceled["rendered"]["gaussian_panel"]["open"] is False
            assert canceled["rendered"]["gaussian_panel"]["exit_reason"] == "cancel"
            no_transient(canceled, directory, original)
            reopen = checkpoints["reopened"]["rendered"]["gaussian_panel"]
            assert reopen["open"] is True and reopen["stack_prompt"] is False
            assert reopen["sigma"] == 0.5 and reopen["sigma_text"] == "0.5"
            assert reopen["scaled_units"] is reopen["preview"] is False
            assert reopen["remembered"] == {"sigma": 0.5, "scaled_units": False}
            expected_final = {"source": (before, original_planes)}
        else:
            assert prompt["actual_native_jobs"] == [] and traces == {}
            output = evidence["oracles"]["u8-full-s2"]["output"]
            planes = [output]*3 if all_z else [original, output, original]
            applied, job_id = changed(checkpoints["source_applied"], before, directory, planes, 2, all_z=all_z)
            assert job_id == only_job["id"]
            if suffix == "focused-no-enter":
                focused = checkpoints["no_focused"]
                same_state(focused["native_state"]["documents"]["source"], before, directory, original_planes)
                no_transient(focused, directory, original)
                no = actual_control(focused["rendered"], "gaussian_panel.controls.stack_no")
                assert no["focused"] is True and focused["rendered"]["input_frame"]["focus"]["nav_id"] == no["id"]
                current = applied
            else:
                history_step(checkpoints["undone"], before, applied, directory, original_planes, redo=False)
                current = history_step(checkpoints["redone"], before, applied, directory, planes, redo=True)
            expected_final = {"source": (current, planes)}
    for name in ("final", "cleanup"):
        point = checkpoints[name]
        assert point["rendered"]["gaussian_panel"]["open"] is False
        assert point["rendered"]["gaussian_panel"]["remembered"] == {
            "sigma": 2 if remembered or not cancel else 0.5, "scaled_units": False}
        for role, (expected, planes) in expected_final.items():
            same_state(point["native_state"]["documents"][role], expected, directory, planes)
        active_role = "fresh_source" if remembered else "source"
        _, planes = expected_final[active_role]
        z = point["native_state"]["documents"][active_role]["display"]["crosshair"][2]
        no_transient(point, directory, planes[z], role=active_role)
    global_state(row["after"], "fresh_source" if remembered else "source")
    for role, (expected, planes) in expected_final.items():
        same_state(row["after"]["documents"][role], expected, directory, planes)
    assert set(row["original_after_raw"]) == set(expected_final)
    for role, members in row["original_after_raw"].items():
        count = len(expected_final[role][1])
        assert [item["position"] for item in members] == [[0, 0, z] for z in range(count)]
        assert all(raw(item["raw"], directory) == original for item in members)
    usage(row["final_cleanup"]["usage"], empty=True)
    current_host = "fresh" if remembered else "primary"
    assert row["final_cleanup"]["actual_native_jobs"] == host_final[current_host]["actual_native_jobs"]
    assert row["final_cleanup"]["observer_retained_result_aliases"] == 0
    assert type(row["final_cleanup"]["observer_retained_result_aliases"]) is int
    assert row["final_cleanup"]["renderer_owns_native_jobs"] is False
    expected_lifecycle = [(host, label) for host in source_ids for label in ("opened", "before_close", "closed")]
    assert [(item["host"], item["label"]) for item in row["lifecycle"]] == expected_lifecycle
    for host in source_ids:
        opened, closing, closed = [item["observed"] for item in row["lifecycle"] if item["host"] == host]
        assert opened["open"] is closing["open"] is True and closed["open"] is False
        assert opened["session_id"] == closing["session_id"] == closed["session_id"] == sessions[host][0]["session_id"]
        assert opened["context_id"] == closing["context_id"] == sessions[host][0]["context_id"] > 0
        assert closed["context_id"] == closed["current_context_id"] == 0
    assert row["final_cleanup"]["lifecycle"] == row["lifecycle"][-1]["observed"]


@pytest.fixture(scope="module")
def stack_cpp_report():
    configured = os.environ.get("IMAGEJXX_STACK_CPP_REPORT")
    if not configured:
        if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
            pytest.fail("Required installed standalone stack report missing: IMAGEJXX_STACK_CPP_REPORT")
        pytest.skip("Installed standalone stack report is optional outside a required desktop gate")
    path = Path(configured).resolve(strict=True)
    report = decode(path.read_bytes())
    assert type(report["schema_version"]) is int and report["schema_version"] == 1
    assert report["context"] == "imgui_cpp" and report["status"] == "passed" and report["acceptance_claim"] is False
    assert report["contract_sha256"] == UI_CONTRACT_SHA and report["groups"] == list(GROUPS)
    assert all(report[field] is False for field in ("full61_verified", "full122_verified", "family_complete",
                                                   "release_accepted", "nonfinite_image_workload_qualified"))
    assert [item["case_id"] for item in report["cases"]] == VARIANTS
    return report, path.parent


def python_row(variant, directory):
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
        desktop = importlib.import_module("imagejxx._desktop")
    else:
        desktop = pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    assert Path(desktop.__file__).suffix in {".pyd", ".so"}
    editor = desktop.Editor(preferences_path=str(directory / "preferences.json"))
    return decode(editor._gaussian_stack_case(variant=variant, context="imgui_python", directory=str(directory)))


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("variant", VARIANTS)
def test_installed_gaussian_stack_ui(variant, context, request, tmp_path, stack_oracles):
    if context == "imgui_cpp":
        report, report_root = request.getfixturevalue("stack_cpp_report")
        row = next(item for item in report["cases"] if item["case_id"] == variant)
        directory = report_root / f"case{VARIANTS.index(variant)+1:02}"
    else:
        directory = tmp_path / "run"
        assert not directory.exists()
        row = python_row(variant, directory)
    assert_row(row, variant, context, directory, stack_oracles)

