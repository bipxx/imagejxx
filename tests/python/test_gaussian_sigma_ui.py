"""Independent installed shared ImGui observations for Gaussian groups02--06.

The expected input is an independently authored analytic fixture. Expected
outputs are unmodified, pinned reference capture bytes, including all Float32
bits. This reader supplies no upstream dialog, GPU, whole-family or release
qualification, and never uses producer PASS text as a numerical oracle.
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
    "P02a.shared_ui.02.sigma-valid-typing": ["sigma-0p5", "sigma-12"],
    "P02a.shared_ui.03.sigma-invalid-rejection": [
        "empty", "malformed", "negative", "nan", "infinity",
        "negative-infinity", "overflow", "unsafe-finite-halo"],
    "P02a.shared_ui.04.zero-sigma-history": ["u8", "u16", "f32", "rgb"],
    "P02a.shared_ui.05.scaled-anisotropy": ["u8", "f32"],
    "P02a.shared_ui.06.pixel-unit-aliases": ["px", "pixel", "pixels"],
}
VARIANTS = [group+"."+suffix for group, suffixes in GROUPS.items() for suffix in suffixes]
CONTEXTS = ["imgui_cpp", "imgui_python"]
UI_CONTRACT_SHA = "2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255"
NUMERIC_CONTRACT_SHA = "3b3f040e01b93642d2b0d3d84db9574f7a9ee525b3a7f29dac7ad21ea03ea6c3"
REFERENCE_SHA = "8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762"
TYPES = {"U8": ("uint8", "B", 1, "UInt8", 1),
         "U16": ("uint16", "H", 2, "UInt16LE", 1),
         "F32": ("float32", "f", 4, "IEEE754 Float32LE", 1),
         "RGB": ("uint8", "B", 1, "UInt8", 3)}
INPUT_HASHES = {
    "U8": "f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4",
    "U16": "133aeaee8e454bcc4fc6d27be46034f670bf9fe8127e6217a4324722555e5323",
    "F32": "7b334761d36148554ea3ab8743b4df82453d1b639d3a2fc8a8d20d1174baffd6",
    "RGB": "f783a5242a27c90c40dcc59916cb52b1c1a2458dd23f9372cab7a9932137eebb",
}
OUTPUT_HASHES = {
    "u8-full-s0": INPUT_HASHES["U8"],
    "u8-full-s0p5": "c8e467e214f036adec4f1ff169bfa41091acbc17d30e922974b751feec632a53",
    "u8-full-s2": "3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b",
    "u8-full-s12": "0693443fe207a6841cd12caf9a97ea811d3995193904dd0464993041119cfc76",
    "u8-scaled-s2-px0p5-py2-run-scaled-harness":
        "9c5c2ba837d156d0b25f8f73ddb7f7da3291037cd522f74b8d9b56f8f0e253ad",
    "u16-full-s0": INPUT_HASHES["U16"],
    "f32-full-s0": INPUT_HASHES["F32"],
    "f32-scaled-s2-px0p5-py2-run-scaled-harness":
        "2dfde4306041c87bd233e00d60bf0a4b34d3a0fd0780950d434742e0174e1e96",
    "rgb-full-s0": INPUT_HASHES["RGB"],
}
PIXEL_CALIBRATION = {"spacing": [1, 1, 1], "origin": [0, 0, 0],
                     "direction": [1, 0, 0, 0, 1, 0, 0, 0, 1], "unit": "pixels",
                     "frame_interval": 1, "time_origin": 0, "time_unit": "s"}
SCALED_CALIBRATION = {**PIXEL_CALIBRATION, "spacing": [0.5, 2, 1], "unit": "um"}
FIXTURE_FORMULA = (
    "owned asymmetric-analytic-v1: b=(37*x+61*y+13*x*y+19*(x%3)+23*(y%5))%256; "
    "UInt8 edge offsets47/83; UInt16=(257*b+97*x+211*y+331*(x%7))%65536; "
    "Float32=(b-127)/8+(x-2*y)/16+((x*y)%17)/256; "
    "RGB=(b+3*x,5*b+11*y+17,255-b+7*x+19*y)%256")


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


def fixture_values(dtype):
    values = []
    for y in range(17):
        for x in range(23):
            base = (37*x+61*y+13*x*y+19*(x % 3)+23*(y % 5)) % 256
            if dtype == "RGB":
                values.extend(((base+3*x) % 256, (5*base+11*y+17) % 256,
                               (255-base+7*x+19*y) % 256))
            elif dtype == "U16":
                values.append((257*base+97*x+211*y+331*(x % 7)) % 65536)
            elif dtype == "F32":
                values.append((base-127)/8+(x-2*y)/16+((x*y) % 17)/256)
            else:
                assert dtype == "U8"
                values.append((base+(47 if x == 0 else 0)+(83 if y == 16 else 0)) % 256)
    return values


def pack(values, dtype):
    return struct.pack("<"+TYPES[dtype][1]*len(values), *values)


def workspace():
    for parent in Path(__file__).resolve().parents:
        if (parent / "src/desktop/window.cpp").is_file() and (parent / "docs/PLAN.md").is_file():
            return parent
    pytest.fail("Cannot locate adopted shared UI contract; configure its explicit path")


def configured_pin(path_name, hash_name, expected):
    configured, digest = os.environ.get(path_name), os.environ.get(hash_name)
    assert configured and digest, f"Required sigma evidence missing: {path_name}/{hash_name}"
    assert digest == expected, f"Sigma gate must pin the adopted {path_name} input"
    path = Path(configured).resolve(strict=True)
    data = path.read_bytes()
    assert sha(data) == digest
    return data


@pytest.fixture(scope="module")
def sigma_oracles():
    assert os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0") in {"0", "1"}
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") != "1" and not os.environ.get(
            "IMAGEJXX_GAUSSIAN_REFERENCE_JSONL"):
        pytest.skip("Groups02--06 installed UI is optional outside a required desktop gate")
    reference = configured_pin("IMAGEJXX_GAUSSIAN_REFERENCE_JSONL",
                               "IMAGEJXX_GAUSSIAN_REFERENCE_SHA256", REFERENCE_SHA)
    numeric = decode(configured_pin("IMAGEJXX_GAUSSIAN_CONTRACT_JSON",
                                    "IMAGEJXX_GAUSSIAN_CONTRACT_SHA256", NUMERIC_CONTRACT_SHA))
    assert type(numeric["schema_version"]) is int and numeric["schema_version"] == 1
    configured = os.environ.get("IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON")
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
        assert configured, "Required sigma gate must supply IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON"
    path = Path(configured) if configured else workspace() / (
        "build/plan-implementation/p02a-gaussian-command-001/shared-ui-case-contract-002.json")
    ui_raw = path.resolve(strict=True).read_bytes()
    assert sha(ui_raw) == UI_CONTRACT_SHA
    ui = decode(ui_raw)
    groups = [item for item in ui["cases"] if item["id"] in GROUPS]
    variants = [item for group in groups for item in group["variants"]]
    assert [item["id"] for item in variants] == VARIANTS
    assert len(variants) == 19 and sum(len(item["steps"]) for item in variants) == 164
    assert all(item["required_contexts"] == CONTEXTS for item in variants)
    originals = {dtype: pack(fixture_values(dtype), dtype) for dtype in TYPES}
    assert {dtype: sha(data) for dtype, data in originals.items()} == INPUT_HASHES
    selected, ids = {}, set()
    for line in reference.split(b"\n"):
        if not line.strip():
            continue
        record = decode(line)
        assert record["id"] not in ids
        ids.add(record["id"])
        if record["id"] not in OUTPUT_HASHES:
            continue
        dtype = record["dtype"]
        shape = [17, 23, 3] if dtype == "RGB" else [17, 23]
        assert record["shape"] == shape and record["input_formula"] == "owned-asymmetric-analytic-v1"
        source = bytes.fromhex(record["input_raw_hex"])
        output = bytes.fromhex(record["output_raw_hex"])
        assert source == pack(record["input"], dtype) == originals[dtype]
        assert output == pack(record["output"], dtype) and len(output) == len(source)
        assert sha(output) == record["output_sha256"] == OUTPUT_HASHES[record["id"]]
        assert record["raw_reference_output_unmodified"] is True
        assert record["outside_changed_pixels"] == 0 and record["outside_changes"] == []
        assert record["roi"]["processor_bounds"] == [0, 0, 23, 17]
        assert record["roi"]["kind"] == "none" and record["roi"]["is_area"] is False
        assert record["full_plugin_runner_executed"] is record["dialog_executed"] is False
        oracle = ui["numeric_oracles"][record["id"]]
        assert oracle["input_sha256"] == sha(source)
        assert oracle["output_sha256"] == oracle["owned_expected_output_sha256"] == sha(output)
        assert oracle["dtype"] == dtype and oracle["shape"] == shape
        assert oracle["raw_reference_full_output_equals_owned_expected"] is True
        assert oracle["raw_reference_outside_changed_pixels"] == 0
        selected[record["id"]] = {"definition": oracle, "input": source, "output": output, "dtype": dtype}
    assert len(ids) == 332 and set(selected) == set(OUTPUT_HASHES)
    return {"oracles": selected, "variants": {item["id"]: item for item in variants}}


def member_path(directory, name):
    root = directory.resolve(strict=True)
    assert type(name) is str and name
    path = (root / name).resolve(strict=True)
    assert path.is_relative_to(root) and path.is_file()
    return path


def raw(member, directory, dtype, *, shape=None):
    native_type, _, size, encoding, channels = TYPES[dtype]
    shape = ([17, 23, channels] if channels > 1 else [17, 23]) if shape is None else shape
    path = member_path(directory, member["member_id"])
    configured = Path(member["path"])
    if not configured.is_absolute():
        configured = directory / configured
    assert configured.resolve(strict=True) == path
    count = math.prod(shape)
    assert member["shape"] == shape and member["dtype"] == native_type
    assert member["encoding"] == encoding
    assert type(member["sample_count"]) is type(member["byte_count"]) is int
    assert member["sample_count"] == count and member["byte_count"] == count*size
    data = path.read_bytes()
    assert len(data) == count*size and sha(data) == member["sha256"]
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
    assert type(value["workflow"]) is type(value["results"]) is list


def document(value, directory, expected, dtype):
    native_type, _, _, _, channels = TYPES[dtype]
    integer(value["document_id"], positive=True)
    integer(value["data_identity"])
    assert value["shape_tcz_yxs"] == [1, 1, 1, 17, 23, channels] and value["dtype"] == native_type
    assert type(value["dirty"]) is bool
    integer(value["undo_steps"])
    integer(value["redo_steps"])
    assert set(value["revisions"]) == {"pixels", "metadata", "selection", "display"}
    for item in value["revisions"].values():
        integer(item)
    assert set(value["calibration"]) == set(PIXEL_CALIBRATION)
    calibration(value["calibration"])
    assert value["history"]["undo_steps"] == value["undo_steps"]
    assert value["history"]["redo_steps"] == value["redo_steps"]
    assert value["history"]["pixels"]["exact"] is True
    for key, item in value["history"]["pixels"].items():
        if key != "exact":
            integer(item)
    assert len(value["planes"]) == 1 and value["planes"][0]["position"] == [0, 0, 0]
    plane = value["planes"][0]
    assert raw(plane["raw"], directory, dtype) == expected
    assert all(type(plane[key]) is str and int(plane[key], 16) > 0 for key in ("image_address", "buffer_address"))
    info = value["document_info"]
    assert info["id"] == value["document_id"] and info["dtype"] == native_type
    assert info["dirty"] is value["dirty"]
    assert info["undo_steps"] == value["undo_steps"] and info["redo_steps"] == value["redo_steps"]


def display_range(dtype):
    return [-32, 64] if dtype == "F32" else [0, 65535] if dtype == "U16" else [0, 255]


def calibration(value):
    assert set(value) == set(PIXEL_CALIBRATION)
    for field, size in (("spacing", 3), ("origin", 3), ("direction", 9)):
        assert type(value[field]) is list and len(value[field]) == size
        assert all(type(item) in {int, float} and math.isfinite(item) for item in value[field])
    assert all(item > 0 for item in value["spacing"])
    for field in ("frame_interval", "time_origin"):
        assert type(value[field]) in {int, float} and math.isfinite(value[field])
    assert value["frame_interval"] > 0
    assert type(value["unit"]) is type(value["time_unit"]) is str


def seeded(value, directory, oracle, calibration, role):
    document(value, directory, oracle["input"], oracle["dtype"])
    assert value["calibration"] == calibration
    assert value["dirty"] is False and value["undo_steps"] == value["redo_steps"] == 0
    assert value["selection"] is value["previous_selection"] is None
    assert value["rois"] == value["overlays"] == [] and value["spectrum_present"] is False
    assert value["metadata"] == {"title": "Owned Gaussian sigma "+role, "properties": {},
                                 "extensions": {}, "record_layout": {}}
    assert value["display"]["properties"]["display_range"] == display_range(oracle["dtype"])
    assert value["display"]["t"] == value["display"]["c"] == 0
    assert value["display"]["crosshair"] == [11, 8, 0] and value["display"]["orthogonal"] is False


def preserved(value, baseline):
    for field in ("document_id", "shape_tcz_yxs", "dtype", "calibration", "selection", "previous_selection",
                  "rois", "overlays", "spectrum_present", "metadata", "display"):
        assert value[field] == baseline[field], f"Gaussian changed preserved native {field}"
    for field in ("metadata", "selection", "display"):
        assert value["revisions"][field] == baseline["revisions"][field]


def unchanged(value, baseline, directory, expected, dtype):
    document(value, directory, expected, dtype)
    assert set(value) == set(baseline)
    for key in baseline:
        if key != "planes":
            assert value[key] == baseline[key], f"Unchanged Gaussian state differs in {key}"
    for actual, before in zip(value["planes"], baseline["planes"]):
        assert {key: item for key, item in actual.items() if key != "raw"} == {
            key: item for key, item in before.items() if key != "raw"}


def cpu_readback(checkpoint, directory, expected, dtype):
    ordinary = checkpoint["rendered"]["ordinary_pixels"]
    assert ordinary["pixels_source"] == ordinary["contrast_source"] == "live"
    readback = ordinary["readback"]
    assert readback["available"] is True
    native_type, fmt, _, _, channels = TYPES[dtype]
    source = readback["source"]
    assert source["dtype"] == native_type and source["shape"] == [17, 23, channels]
    assert "pixels" not in source and "no GPU readback" in source["observation_scope"]
    assert raw(source["raw"], directory, dtype, shape=[17, 23, channels]) == expected
    samples = struct.unpack("<"+fmt*(17*23*channels), expected)
    low, high = display_range(dtype)
    mapped = [math.floor(min(255, max(0, 255/(high-low)*sample-low*255/(high-low)))+0.5)
              for sample in samples]
    rgb_expected = bytes(mapped) if channels == 3 else bytes(channel for sample in mapped for channel in (sample,)*3)
    rgb = readback["rendered_rgb"]
    assert rgb["dtype"] == "uint8" and rgb["shape"] == [17, 23, 3]
    assert "pixels" not in rgb and "no GPU readback" in rgb["observation_scope"]
    assert raw(rgb["raw"], directory, "RGB") == rgb_expected


def actual_control(rendered, selector, document_id=None):
    if selector.startswith("gaussian_panel.controls."):
        name = selector.removeprefix("gaussian_panel.controls.")
        value = dict(rendered["gaussian_panel"]["control_states"][name])
        value["rect"] = rendered["gaussian_panel"]["control_rects"][name]
        value["point"] = rendered["gaussian_panel"]["controls"][name]
        return value
    if selector == "document_tabs.new_document":
        matches = [item for item in rendered["document_tabs"] if item["document_id"] == document_id]
        assert len(matches) == 1
        return matches[0]
    assert selector in {"menu.Edit", "menu.Edit.Undo", "menu.Edit.Redo"}
    return rendered["host_controls"][selector]


def physical_frames(row, directory, adopted):
    frames = row["frames"]
    assert frames and len(frames) <= 1200
    assert [item["ordinal"] for item in frames] == list(range(1, len(frames)+1))
    sessions = []
    for frame in frames:
        assert decode(member_path(directory, f"frame-{frame['ordinal']}.json").read_bytes()) == frame
        usage(frame["usage"])
        jobs(frame["actual_native_jobs"])
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
    steps = row["steps"]
    assert [item["ordinal"] for item in steps] == list(range(1, len(adopted["steps"])+1))
    assert [item["adopted_step"] for item in steps] == adopted["steps"]
    previous_last = 0
    for step in steps:
        first, last = step["first_frame_ordinal"], step["last_frame_ordinal"]
        integer(first)
        integer(last)
        assert previous_last <= first <= last <= len(frames)
        previous_last = last
        assert step["completed"] is True and step["before_frame_ordinal"] == first
        if step["adopted_step"]["op"] == "observe":
            assert first == last, "An observation must not silently inject another input frame"
        for frame in frames[first:last]:
            assert frame["step_ordinal"] == step["ordinal"]
    invalid = row["group_id"] == "P02a.shared_ui.03.sigma-invalid-rejection"
    for observation in row["control_observations"]:
        prior = integer(observation["before_frame_ordinal"], positive=True)
        assert prior+3 <= len(frames)
        control = observation["control"]
        integer(control["id"], positive=True)
        assert type(control["enabled"]) is type(control["active"]) is type(control["focused"]) is bool
        attempted_disabled = invalid and observation["selector"] == "gaussian_panel.controls.ok"
        assert observation["attempted_disabled"] is attempted_disabled
        assert control["enabled"] is not attempted_disabled
        assert control == actual_control(frames[prior-1]["rendered"], observation["selector"], control.get("document_id"))
        x, y = observation["position"]
        rect = control["rect"]
        assert len(rect) == 4 and all(type(item) in {int, float} and math.isfinite(item) for item in rect)
        assert x == math.floor(x) and y == math.floor(y)
        assert rect[0] <= x < rect[2] and rect[1] <= y < rect[3]
        assert [item["requested_events"] for item in frames[prior:prior+3]] == [
            [{"type": "position", "space": "screen", "point": [x, y]}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}],
        ]
        positioned = actual_control(frames[prior]["rendered"], observation["selector"], control.get("document_id"))
        assert positioned["id"] == control["id"] and positioned["enabled"] is control["enabled"]
        current_rect = positioned["rect"]
        assert current_rect[0] <= x < current_rect[2] and current_rect[1] <= y < current_rect[3]
    # Native jobs can be admitted only after the real enabled OK release.
    # Diagnostic declarations or a preparatory action cannot substitute for it.
    accepts = [item["before_frame_ordinal"]+3 for item in row["control_observations"]
               if item["selector"] == "gaussian_panel.controls.ok" and not item["attempted_disabled"]]
    for frame in frames:
        admitted = sum(frame["ordinal"] >= released for released in accepts)
        assert len(jobs(frame["actual_native_jobs"])) <= admitted
    return frames


def physical_steps(row, frames, adopted, calibration):
    observations = row["control_observations"]
    used = set()
    api = [item for item in row["api_inputs"] if item["purpose"] != "bounded final/failure native job cleanup only"]
    preps = [item for item in api if item["step_ordinal"] == 0]
    assert len(preps) == 1
    source_prep = preps[0]
    assert source_prep["role"] == "source" and source_prep["input_oracle"] == adopted["setup"]["input_oracle"]
    assert source_prep["purpose"] == "explicit fresh UNBLURRED fixture/calibration preparation"
    assert source_prep["methods"] == ["addNativeDocument", "setNativeDisplay", "mark_saved"]
    assert source_prep["calibration"] == calibration and source_prep["tested_changes_use_API_setters"] is False
    integer(source_prep["document_id"], positive=True)
    cleanups = [item for item in row["api_inputs"] if item["purpose"] == "bounded final/failure native job cleanup only"]
    assert cleanups == [{"step_ordinal": len(adopted["steps"]), "purpose": "bounded final/failure native job cleanup only",
                         "methods": ["Window.cancel", "action(wait_jobs,timeout_ms=5000)", "Window.poll"]}]
    allowed = {0} | {step["ordinal"] for step in row["steps"]
                     if step["adopted_step"]["op"] in {"open_command", "api_preparation"}}
    assert all(item["step_ordinal"] in allowed for item in api)
    invalid = row["group_id"] == "P02a.shared_ui.03.sigma-invalid-rejection"
    for step in row["steps"]:
        declaration, ordinal = step["adopted_step"], step["ordinal"]
        operation = declaration["op"]
        actual = frames[step["first_frame_ordinal"]:step["last_frame_ordinal"]]
        clicks = [item for item in observations if item["step_ordinal"] == ordinal]
        prep = [item for item in api if item["step_ordinal"] == ordinal]
        used.update(observations.index(item) for item in clicks)
        if operation == "click":
            assert len(clicks) == 1 and clicks[0]["selector"] == declaration["selector"] and not prep
            if declaration["selector"] == "gaussian_panel.controls.scaled":
                before = frames[step["first_frame_ordinal"]-1]["rendered"]["gaussian_panel"]
                after = actual[-1]["rendered"]["gaussian_panel"]
                assert before["scaled_available"] is after["scaled_available"] is True
                assert before["scaled_units"] is False and after["scaled_units"] is True
            if declaration["selector"] == "document_tabs.new_document":
                new_id = integer(clicks[0]["control"]["document_id"], positive=True)
                assert actual[-1]["rendered"]["document_info"]["id"] == new_id
        elif operation in {"actual_undo", "actual_redo"}:
            assert not prep
            assert [item["selector"] for item in clicks] == [
                "menu.Edit", "menu.Edit."+("Undo" if operation == "actual_undo" else "Redo")]
        elif operation == "replace_sigma":
            assert len(clicks) == 1 and clicks[0]["selector"] == "gaussian_panel.controls.sigma" and not prep
            input_id = clicks[0]["control"]["id"]
            keyed = [event for frame in actual for event in frame["requested_events"] if event["type"] in {"key", "keys"}]
            expected = [
                {"type": "keys", "keys": [{"name": "Ctrl", "down": True}, {"name": "A", "down": True}]},
                {"type": "keys", "keys": [{"name": "Ctrl", "down": False}, {"name": "A", "down": False}]},
            ]
            if declaration["text"] == "":
                expected += [{"type": "key", "name": "Backspace", "down": True},
                             {"type": "key", "name": "Backspace", "down": False}]
            assert keyed == expected
            texts = [frame for frame in actual if any(event["type"] == "text" for event in frame["requested_events"])]
            assert len(texts) == (1 if declaration["text"] else 0)
            if texts:
                assert texts[0]["requested_events"] == [{"type": "text", "value": declaration["text"]}]
                input_frame = texts[0]["rendered"]["input_frame"]
                assert input_frame["focus"]["active_id"] == input_frame["input_text"]["id"] == input_id
            for frame in actual:
                if any(event["type"] in {"key", "keys"} for event in frame["requested_events"]):
                    assert frame["rendered"]["input_frame"]["focus"]["active_id"] == input_id
            settled = actual[-1]
            assert settled["requested_events"] == [] and not settled["rendered"]["input_frame"]["keys_down"]
            panel = settled["rendered"]["gaussian_panel"]
            assert panel["sigma_text"] == declaration["text"] and panel["preview"] is False
            assert panel["draft_valid"] is not invalid
            if not invalid:
                assert panel["sigma"] == float(declaration["text"])
        elif operation == "key_pulse":
            assert declaration["name"] == "Enter" and invalid and not clicks and not prep
            events = [event for frame in actual for event in frame["requested_events"]]
            assert events == [{"type": "key", "name": "Enter", "down": True},
                              {"type": "key", "name": "Enter", "down": False}]
            assert actual[-1]["requested_events"] == []
        elif operation == "open_command":
            assert not clicks and len(prep) == 1
            assert prep[0] == {"step_ordinal": ordinal, "purpose": "adopted labelled_action_preparation opening only",
                               "action": "show_operation", "parameters": {"operation": "gaussian_blur_imagej"}}
            panel = actual[-1]["rendered"]["gaussian_panel"]
            assert panel["open"] is True and panel["preview"] is False
            if ordinal == 1:
                assert panel["sigma"] == 2 and panel["scaled_units"] is False
                assert panel["remembered"] == {"sigma": 2, "scaled_units": False}
                assert panel["scaled_available"] is (calibration["unit"] == "um")
        elif operation == "api_preparation":
            assert declaration["name"] == "create_second_document" and not clicks and len(prep) == 1
            item = prep[0]
            assert item["purpose"] == "explicit fresh UNBLURRED fixture/calibration preparation"
            assert item["methods"] == ["addNativeDocument", "setNativeDisplay", "mark_saved"]
            assert item["role"] == "new_document" and item["input_oracle"] == "u8-full-s2"
            assert item["calibration"] == {**SCALED_CALIBRATION, "unit": declaration["calibration"]["unit"]}
            assert item["tested_changes_use_API_setters"] is False
            integer(item["document_id"], positive=True)
        else:
            assert operation in {"observe", "await_native"} and not clicks and not prep
            assert not any(frame["requested_events"] for frame in actual)
            if operation == "await_native":
                assert declaration == {"op": "await_native", "condition": "gaussian_apply_terminal",
                                       "watchdog_seconds": 30, "max_pump_frames": 1200}
    assert used == set(range(len(observations))), "Unreviewed physical input or API setter substitution"


def expected_record(record, before, sigma, scaled):
    assert set(record) == {"schema_version", "operation", "options", "calibration", "effective_sigma_xy", "target"}
    assert type(record["schema_version"]) is int and record["schema_version"] == 1
    assert record["operation"] == "gaussian_blur_imagej"
    assert record["options"] == {"sigma": sigma, "scaled_units": scaled}
    assert type(record["options"]["sigma"]) in {int, float} and type(record["options"]["scaled_units"]) is bool
    assert record["calibration"] == before["calibration"]
    calibration(record["calibration"])
    effective = [sigma/before["calibration"]["spacing"][axis] if scaled else sigma for axis in (0, 1)]
    assert record["effective_sigma_xy"] == effective
    assert type(record["effective_sigma_xy"]) is list and all(
        type(item) in {int, float} and math.isfinite(item) for item in record["effective_sigma_xy"])
    target = record["target"]
    assert target == {"schema_version": 1, "document_id": before["document_id"],
                      "source_revisions": [before["revisions"][key] for key in ("pixels", "metadata", "selection")],
                      "source_shape": before["shape_tcz_yxs"], "scope": "ranges",
                      "ranges": [[0, 1, 1], [0, 1, 1], [0, 1, 1]], "positions": [],
                      "roi_policy": "active_selection", "execution_mode": "planes", "destination": "in_place"}
    assert type(target["schema_version"]) is int
    integer(target["document_id"], positive=True)
    for values in [target["source_revisions"], target["source_shape"], *target["ranges"]]:
        for value in values:
            integer(value)


def apply_observation(checkpoint, baseline, before, sigma, scaled):
    lane = checkpoint["rendered"]["gaussian_jobs"]["apply"]
    assert lane["exit"] == "published" and lane["error"] == "" and lane["owns_job"] is False
    assert lane["source_id"] == lane["published_document_id"] == before["document_id"]
    expected_record(lane["command_record"], before, sigma, scaled)
    assert lane["target"] == lane["command_record"]["target"]
    # Only Preview has a request generation. Apply lineage is the actual job,
    # captured command and destination publication; do not invent a generation.
    assert lane["request_generation"] is None
    observed = lane["job"]
    job_id = integer(observed["id"], positive=True)
    previous, current = jobs(baseline["actual_native_jobs"]), jobs(checkpoint["actual_native_jobs"])
    assert set(current)-set(previous) == {job_id}
    actual = current[job_id]
    assert actual["state"] == "COMPLETED" and observed["state"] == "completed"
    for item in (actual, observed):
        assert item["document_id"] == before["document_id"] and item["label"] == "Gaussian Apply"
        assert item["error"] == "" and item["cancel_requested"] is False
        integer(item["resource_bytes"])
    assert checkpoint["rendered"]["gaussian_panel"]["remembered"] == {"sigma": sigma, "scaled_units": scaled}
    assert checkpoint["rendered"]["gaussian_panel"]["open"] is False
    usage(checkpoint["usage"], empty=True)
    return job_id


def changed(value, before, directory, oracle):
    document(value, directory, oracle["output"], oracle["dtype"])
    preserved(value, before)
    assert oracle["output"] != oracle["input"]
    assert value["data_identity"] != before["data_identity"]
    assert value["revisions"]["pixels"] == before["revisions"]["pixels"]+1
    assert value["undo_steps"] == before["undo_steps"]+1 and value["redo_steps"] == 0
    assert value["dirty"] is True


def assert_row(row, variant, context, directory, evidence):
    adopted = evidence["variants"][variant]
    group = variant.rsplit(".", 1)[0]
    invalid = group == "P02a.shared_ui.03.sigma-invalid-rejection"
    identity = group == "P02a.shared_ui.04.zero-sigma-history"
    aliases = group == "P02a.shared_ui.06.pixel-unit-aliases"
    scaled = aliases or group == "P02a.shared_ui.05.scaled-anisotropy"
    calibration = SCALED_CALIBRATION if scaled else PIXEL_CALIBRATION
    assert type(row["schema_version"]) is int and row["schema_version"] == 1
    assert row["group_id"] == group and row["case_id"] == variant and row["context"] == context
    assert row["status"] == "passed" and row["acceptance_claim"] is False
    assert row["contract_sha256"] == UI_CONTRACT_SHA and row["reference_capture_sha256"] == REFERENCE_SHA
    assert row["adopted_variant"] == adopted and row["fixture_formula"] == FIXTURE_FORMULA
    assert all(row[field] is False for field in (
        "full61_verified", "full122_verified", "family_complete", "release_accepted", "nonfinite_image_workload_qualified"))
    assert decode(member_path(directory, "case.json").read_bytes()) == row
    input_oracle = evidence["oracles"][adopted["setup"]["input_oracle"]]
    dtype, original = input_oracle["dtype"], input_oracle["input"]
    assert raw(row["fixture_input_raw"], directory, dtype) == original
    frames = physical_frames(row, directory, adopted)
    physical_steps(row, frames, adopted, calibration)
    checkpoints = {}
    for checkpoint in row["checkpoints"]:
        name = checkpoint["name"]
        assert name not in checkpoints
        assert decode(member_path(directory, "checkpoint-"+name+".json").read_bytes()) == checkpoint
        assert checkpoint["rendered"] == frames[checkpoint["frame_ordinal"]-1]["rendered"]
        usage(checkpoint["usage"])
        jobs(checkpoint["actual_native_jobs"])
        global_state(checkpoint["native_state"])
        checkpoints[name] = checkpoint
    expected_names = {"baseline", "cleanup"}
    occurrence = {}
    for step in row["steps"]:
        declaration = step["adopted_step"]
        if declaration["op"] == "observe":
            label = declaration["label"]
            occurrence[label] = occurrence.get(label, 0)+1
            name = ("source_applied" if occurrence[label] == 1 else "new_document_applied") if label == "applied" else label
            expected_names.add(name)
            checkpoint = checkpoints[name]
            assert step["checkpoint_name"] == name and checkpoint["label"] == label
            assert checkpoint["occurrence"] == occurrence[label]
            assert checkpoint["step_ordinal"] == step["ordinal"] and checkpoint["frame_ordinal"] == step["last_frame_ordinal"]
        elif declaration["op"] == "api_preparation":
            expected_names.add("new_document_baseline")
            assert step["checkpoint_name"] is None
        else:
            assert step["checkpoint_name"] is None
    assert set(checkpoints) == expected_names
    baseline = checkpoints["baseline"]
    state0 = baseline["native_state"]
    assert set(state0["documents"]) == {"source"}
    before = state0["documents"]["source"]
    seeded(before, directory, input_oracle, calibration, "source")
    assert state0["recording"] is False and state0["workflow"] == state0["results"] == []
    assert jobs(baseline["actual_native_jobs"]) == {}
    usage(baseline["usage"], empty=True)
    assert row["oracles"]["source"]["input"] == input_oracle["definition"]
    source_prep = next(item for item in row["api_inputs"] if item["step_ordinal"] == 0)
    assert source_prep["document_id"] == before["document_id"]
    cpu_readback(baseline, directory, original, dtype)
    for frame in frames:
        lane = frame["rendered"]["gaussian_jobs"]["preview"]
        assert lane["owns_job"] is False and lane["job"] is None
        panel = frame["rendered"]["gaussian_panel"]
        assert panel["preview"] is False and panel["preview_requested"] is False
    final_expected = {"source": original}
    if invalid:
        assert row["oracles"]["source"]["output"] is None
        for name in ("invalid_draft", "attempted_disabled_accept"):
            point = checkpoints[name]
            panel = point["rendered"]["gaussian_panel"]
            assert panel["open"] is True and panel["visible"] is True
            assert panel["sigma_text"] == adopted["typed_sigma"] and panel["draft_valid"] is False
            assert type(panel["error"]) is str and panel["error"]
            assert panel["control_states"]["ok"]["enabled"] is False
            assert panel["control_states"]["preview"]["enabled"] is False
            assert panel["control_states"]["sigma"]["enabled"] is panel["control_states"]["cancel"]["enabled"] is True
            assert panel["remembered"] == {"sigma": 2, "scaled_units": False}
            unchanged(point["native_state"]["documents"]["source"], before, directory, original, dtype)
            cpu_readback(point, directory, original, dtype)
        for point in checkpoints.values():
            assert jobs(point["actual_native_jobs"]) == {}
            unchanged(point["native_state"]["documents"]["source"], before, directory, original, dtype)
        for frame in frames:
            assert jobs(frame["actual_native_jobs"]) == {}
            assert frame["rendered"]["gaussian_jobs"]["apply"]["job"] is None
            assert frame["rendered"]["gaussian_jobs"]["apply"]["published_document_id"] is None
    else:
        sigma = 0 if identity else float(adopted.get("typed_sigma", "2"))
        source_output_id = adopted["setup"]["input_oracle"] if aliases else adopted["output_oracle"]
        output_oracle = evidence["oracles"][source_output_id]
        assert row["oracles"]["source"]["output"] == output_oracle["definition"]
        applied = checkpoints["source_applied"]
        apply_observation(applied, baseline, before, sigma, scaled)
        value = applied["native_state"]["documents"]["source"]
        if identity:
            assert output_oracle["output"] == original
            unchanged(value, before, directory, original, dtype)
            reopened = checkpoints["reopened"]["rendered"]["gaussian_panel"]
            assert reopened["open"] is True and reopened["sigma"] == 0 and reopened["sigma_text"] == "0"
            assert reopened["remembered"] == {"sigma": 0, "scaled_units": False}
            assert reopened["scaled_units"] is False and reopened["source_id"] == before["document_id"]
            for point in checkpoints.values():
                unchanged(point["native_state"]["documents"]["source"], before, directory, original, dtype)
        else:
            changed(value, before, directory, output_oracle)
        final_expected["source"] = output_oracle["output"]
        cpu_readback(applied, directory, final_expected["source"], dtype)
        if group == "P02a.shared_ui.02.sigma-valid-typing":
            draft = checkpoints["valid_draft"]["rendered"]["gaussian_panel"]
            assert draft["sigma_text"] == adopted["typed_sigma"] and draft["sigma"] == sigma
            assert draft["draft_valid"] is True and draft["control_states"]["ok"]["enabled"] is True
            assert draft["error"] == ""
            unchanged(checkpoints["valid_draft"]["native_state"]["documents"]["source"],
                      before, directory, original, dtype)
            undone = checkpoints["undone"]["native_state"]["documents"]["source"]
            redone = checkpoints["redone"]["native_state"]["documents"]["source"]
            document(undone, directory, original, dtype)
            document(redone, directory, output_oracle["output"], dtype)
            preserved(undone, before)
            preserved(redone, before)
            assert undone["data_identity"] == before["data_identity"] and undone["dirty"] is False
            assert undone["undo_steps"] == 0 and undone["redo_steps"] == 1
            assert redone["data_identity"] == value["data_identity"] and redone["dirty"] is True
            assert redone["undo_steps"] == 1 and redone["redo_steps"] == 0
            assert undone["revisions"]["pixels"] == value["revisions"]["pixels"]+1
            assert redone["revisions"]["pixels"] == undone["revisions"]["pixels"]+1
            cpu_readback(checkpoints["undone"], directory, original, dtype)
            cpu_readback(checkpoints["redone"], directory, output_oracle["output"], dtype)
        if group == "P02a.shared_ui.05.scaled-anisotropy":
            draft = checkpoints["scaled_draft"]["rendered"]["gaussian_panel"]
            assert draft["scaled_available"] is draft["scaled_units"] is True
            assert draft["control_states"]["scaled"]["enabled"] is True
            assert draft["sigma"] == 2
            unchanged(checkpoints["scaled_draft"]["native_state"]["documents"]["source"],
                      before, directory, original, dtype)
        if aliases:
            new_baseline = checkpoints["new_document_baseline"]
            second_before = new_baseline["native_state"]["documents"]["new_document"]
            alias_calibration = {**SCALED_CALIBRATION, "unit": variant.rsplit(".", 1)[1]}
            second_oracle = evidence["oracles"]["u8-full-s2"]
            seeded(second_before, directory, second_oracle, alias_calibration, "new_document")
            unchanged(new_baseline["native_state"]["documents"]["source"], value,
                      directory, final_expected["source"], dtype)
            assert second_before["document_id"] != before["document_id"]
            new_prep = next(item for item in row["api_inputs"] if item.get("role") == "new_document")
            assert new_prep["document_id"] == second_before["document_id"]
            pixel_units = checkpoints["pixel_units"]["rendered"]["gaussian_panel"]
            assert pixel_units["source_id"] == second_before["document_id"] and pixel_units["open"] is True
            assert pixel_units["scaled_available"] is pixel_units["scaled_units"] is False
            assert "scaled" not in pixel_units["controls"] and "scaled" not in pixel_units["control_states"]
            assert pixel_units["remembered"] == {"sigma": 2, "scaled_units": True}
            unchanged(checkpoints["pixel_units"]["native_state"]["documents"]["new_document"],
                      second_before, directory, second_oracle["input"], "U8")
            unchanged(checkpoints["pixel_units"]["native_state"]["documents"]["source"],
                      value, directory, final_expected["source"], dtype)
            second = checkpoints["new_document_applied"]
            apply_observation(second, new_baseline, second_before, 2, False)
            changed(second["native_state"]["documents"]["new_document"], second_before, directory, second_oracle)
            unchanged(second["native_state"]["documents"]["source"], value, directory, final_expected["source"], dtype)
            cpu_readback(second, directory, second_oracle["output"], "U8")
            final_expected["new_document"] = second_oracle["output"]
            assert row["oracles"]["new_document"] == {"input": second_oracle["definition"], "output": second_oracle["definition"]}
    assert set(row["oracles"]) == set(final_expected)
    final = row["after"]
    global_state(final)
    assert set(final["documents"]) == set(final_expected)
    for state in [point["native_state"] for point in checkpoints.values()]+[final]:
        assert state["recording"] is False and state["workflow"] == state0["workflow"]
        assert state["results"] == state0["results"]
        preserved(state["documents"]["source"], before)
    for role, expected in final_expected.items():
        document(final["documents"][role], directory, expected, dtype if role == "source" else "U8")
        if invalid or identity:
            expected_final = before
        elif role == "new_document":
            expected_final = checkpoints["new_document_applied"]["native_state"]["documents"][role]
        elif group == "P02a.shared_ui.02.sigma-valid-typing":
            expected_final = checkpoints["redone"]["native_state"]["documents"][role]
        else:
            expected_final = checkpoints["source_applied"]["native_state"]["documents"][role]
        unchanged(final["documents"][role], expected_final, directory, expected,
                  dtype if role == "source" else "U8")
        unchanged(checkpoints["cleanup"]["native_state"]["documents"][role], final["documents"][role],
                  directory, expected, dtype if role == "source" else "U8")
    assert checkpoints["cleanup"]["rendered"]["gaussian_panel"]["open"] is False
    if invalid:
        assert checkpoints["cleanup"]["rendered"]["gaussian_panel"]["remembered"] == {"sigma": 2, "scaled_units": False}
    assert set(row["original_after_raw"]) == set(final_expected)
    for role, member in row["original_after_raw"].items():
        assert raw(member, directory, dtype if role == "source" else "U8") == (
            original if role == "source" else evidence["oracles"]["u8-full-s2"]["input"])
    usage(row["final_cleanup"]["usage"], empty=True)
    final_jobs = jobs(row["final_cleanup"]["actual_native_jobs"])
    assert len(final_jobs) == (0 if invalid else 2 if aliases else 1)
    assert final_jobs == jobs(checkpoints["cleanup"]["actual_native_jobs"])
    assert all(value["state"] == "COMPLETED" and value["label"] == "Gaussian Apply"
               and value["error"] == "" and value["cancel_requested"] is False for value in final_jobs.values())
    assert all(set(jobs(frame["actual_native_jobs"])).issubset(final_jobs) for frame in frames)
    assert [item["label"] for item in row["lifecycle"]] == ["opened", "before_close", "closed"]
    opened, closing, closed = [item["observed"] for item in row["lifecycle"]]
    assert opened["open"] is closing["open"] is True and closed["open"] is False
    assert opened["session_id"] == closing["session_id"] == closed["session_id"]
    assert opened["context_id"] == closing["context_id"] == frames[0]["rendered"]["session"]["context_id"] > 0
    assert closed["context_id"] == closed["current_context_id"] == 0
    assert row["final_cleanup"]["lifecycle"] == closed


@pytest.fixture(scope="module")
def sigma_cpp_report():
    configured = os.environ.get("IMAGEJXX_SIGMA_CPP_REPORT")
    if not configured:
        if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
            pytest.fail("Required installed standalone sigma report missing: IMAGEJXX_SIGMA_CPP_REPORT")
        pytest.skip("Installed standalone sigma report is optional outside a required desktop gate")
    path = Path(configured).resolve(strict=True)
    report = decode(path.read_bytes())
    assert type(report["schema_version"]) is int and report["schema_version"] == 1
    assert report["context"] == "imgui_cpp" and report["status"] == "passed" and report["acceptance_claim"] is False
    assert report["contract_sha256"] == UI_CONTRACT_SHA and report["groups"] == list(GROUPS)
    assert all(report[field] is False for field in ("full61_verified", "full122_verified", "family_complete", "release_accepted",
                                                   "nonfinite_image_workload_qualified"))
    assert [row["case_id"] for row in report["cases"]] == VARIANTS
    return report, path.parent


def python_row(variant, directory):
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
        desktop = importlib.import_module("imagejxx._desktop")
    else:
        desktop = pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    assert Path(desktop.__file__).suffix in {".pyd", ".so"}
    editor = desktop.Editor(preferences_path=str(directory / "preferences.json"))
    return decode(editor._gaussian_sigma_case(variant=variant, context="imgui_python", directory=str(directory)))


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("variant", VARIANTS)
def test_installed_gaussian_sigma_ui(variant, context, request, tmp_path, sigma_oracles):
    if context == "imgui_cpp":
        report, report_root = request.getfixturevalue("sigma_cpp_report")
        row = next(item for item in report["cases"] if item["case_id"] == variant)
        directory = report_root / f"case{VARIANTS.index(variant)+1:02}"
    else:
        directory = tmp_path / "run"
        assert not directory.exists()
        row = python_row(variant, directory)
    assert_row(row, variant, context, directory, sigma_oracles)
