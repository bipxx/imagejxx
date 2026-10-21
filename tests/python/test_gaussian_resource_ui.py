"""Independent actual-input R16 assertions for both installed ImGui launchers.

The 512-square resource fixture is project owned. Native primitive comparison
reconciles the displayed result; it supplies no ImageJ reference or GPU proof.
The producer reports observations before assertions, rather than PASS oracles.
"""

import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import struct

import pytest


VARIANT_ID = "P02a.shared_ui.20.busy-failure-cancel-atomicity.held-preview-lease-lowered-budget"
CONTEXTS = ["imgui_cpp", "imgui_python"]
SHAPE = [512, 512]
SAMPLES = 512 * 512
MIB = 1024 * 1024
USAGE_FIELDS = {"running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"}
SOURCE_FIELDS = ["document_id", "data_identity", "dirty", "undo_steps", "redo_steps",
                 "revisions", "history", "document_info", "calibration", "selection", "rois",
                 "previous_selection", "spectrum_present", "overlays", "metadata",
                 "display", "workflow", "results", "recording",
                 "source_image_address", "source_buffer_address"]


def fixture_bytes():
    return b"".join(struct.pack("<f", ((x + 3*y) % 251)/16 +
                              (512 if (x, y) == (257, 193) else 0))
                    for y in range(512) for x in range(512))


def integer(value, *, positive=False):
    assert type(value) is int and value >= (1 if positive else 0)
    return value


def member_path(root, name):
    root = root.resolve(strict=True)
    path = (root / name).resolve(strict=True)
    assert path.is_relative_to(root) and path.is_file(), "R16 artifact escapes its case directory"
    return path


def raw(member, root, *, shape=SHAPE):
    path = Path(member["path"])
    if not path.is_absolute():
        path = root / path
    path = path.resolve(strict=True)
    assert path.is_relative_to(root.resolve(strict=True)) and path.is_file()
    assert path == member_path(root, member["member_id"])
    assert member["encoding"] == "IEEE754 Float32LE"
    assert member["dtype"] == "float32" and member["shape"] == shape
    assert member["sample_count"] == SAMPLES and member["byte_count"] == SAMPLES * 4
    data = path.read_bytes()
    assert len(data) == member["byte_count"]
    assert hashlib.sha256(data).hexdigest() == member["sha256"]
    assert all(math.isfinite(sample[0]) for sample in struct.iter_unpack("<f", data))
    return data


def usage(value, *, empty=False):
    assert set(value) == USAGE_FIELDS
    for item in value.values():
        integer(item)
    assert value["reserved_bytes"] == value["running_bytes"] + value["retained_bytes"]
    if empty:
        assert not any(value.values())


def control(value):
    integer(value["id"], positive=True)
    assert type(value["enabled"]) is bool
    assert type(value["active"]) is bool and type(value["focused"]) is bool
    assert len(value["rect"]) == 4 and len(value["point"]) == 2
    x0, y0, x1, y1 = value["rect"]
    assert all(type(item) in {int, float} and math.isfinite(item) for item in value["rect"])
    assert x0 < x1 and y0 < y1


def actual_storage(value):
    assert value["schema_version"] == 1 and value["eager"] is True
    assert value["native_sample_storage"] == "double" and value["native_sample_bytes"] == 8
    assert value["measurement_scope"] == "held_preview_eager_buffers_including_shared_source_planes"
    assert value["address_semantics"] == "process_lifetime_local" and value["allocator_ids"] is False
    assert value["pixel_read_policy"] == "no_pixel_value_or_virtual_plane_reads"
    assert value["guard_addresses"] and len(value["guard_addresses"]) == len(set(value["guard_addresses"]))
    assert all(type(address) is str and int(address, 16) > 0 for address in value["guard_addresses"])
    buffers = {}
    images = set()
    assert len(value["planes"]) == 1
    for plane in value["planes"]:
        assert plane["index"] == 0 and [plane["height"], plane["width"]] == SHAPE
        assert plane["channels"] == 1 and plane["pixel_count"] == SAMPLES
        integer(plane["pixel_capacity"])
        assert plane["pixel_capacity"] >= plane["pixel_count"]
        assert plane["pixel_payload_bytes"] == plane["pixel_count"] * 8
        assert plane["pixel_capacity_bytes"] == plane["pixel_capacity"] * 8
        assert plane["guard_tag_present"] is True
        assert plane["guard_address"] in value["guard_addresses"]
        assert int(plane["image_address"], 16) > 0 and int(plane["buffer_address"], 16) > 0
        images.add(plane["image_address"])
        signature = (plane["pixel_payload_bytes"], plane["pixel_capacity_bytes"])
        if plane["buffer_address"] in buffers:
            assert buffers[plane["buffer_address"]] == signature
        buffers[plane["buffer_address"]] = signature
    assert value["unique_image_count"] == len(images)
    assert value["unique_buffer_count"] == len(buffers)
    assert value["pixel_payload_bytes"] == sum(item[0] for item in buffers.values()) == SAMPLES * 8
    assert value["pixel_capacity_bytes"] == sum(item[1] for item in buffers.values())
    assert value["pixel_capacity_bytes"] >= value["pixel_payload_bytes"]
    assert set(value["guard_addresses"]) == {plane["guard_address"] for plane in value["planes"]}
    integer(value["source_document_id"], positive=True)
    integer(value["job_id"], positive=True)
    integer(value["request_generation"], positive=True)


def source(value, root, original):
    integer(value["document_id"], positive=True)
    # Native Document data identities start at zero; compare the exact baseline below.
    integer(value["data_identity"])
    assert type(value["dirty"]) is bool
    integer(value["undo_steps"])
    integer(value["redo_steps"])
    assert set(value["revisions"]) == {"pixels", "metadata", "selection", "display"}
    for revision in value["revisions"].values():
        integer(revision)
    assert raw(value["source_raw"], root) == original
    for key in SOURCE_FIELDS:
        assert key in value, f"R16 source observation omitted {key}"


def unchanged(observed, baseline, root, original):
    source(observed, root, original)
    assert set(observed) == set(baseline)
    for key in baseline:
        if key != "source_raw":
            assert observed[key] == baseline[key], f"R16 unexpectedly changed native {key}"


def preferences(value):
    assert type(value["bytes_utf8"]) is str
    data = value["bytes_utf8"].encode("utf-8")
    assert hashlib.sha256(data).hexdigest() == value["sha256"]
    parsed = json.loads(value["bytes_utf8"])
    assert parsed == value["envelope"]
    assert parsed["format"] == "imagejxx-preferences" and parsed["version"] == 1
    assert type(parsed["values"]["processing_memory_mb"]) is int
    assert type(parsed["extras"]) is dict and type(parsed["extras"]["recent"]) is list
    assert type(value["path"]) is str and value["path"]
    return data


def native_limits(value, memory_mb):
    integer(value["workers"])
    integer(value["capacity"], positive=True)
    assert value["memory_bytes"] == memory_mb * MIB


def cpu_readback(rendered, root, expected, *, preview):
    ordinary = rendered["ordinary_pixels"]
    assert ordinary["pixels_source"] == ("preview" if preview else "live")
    assert ordinary["contrast_source"] == "live"
    readback = ordinary["readback"]
    assert readback["available"] is True
    actual_source = readback["source"]
    assert actual_source["shape"] == [512, 512, 1] and actual_source["dtype"] == "float32"
    assert "pixels" not in actual_source and "no GPU readback" in actual_source["observation_scope"]
    assert raw(actual_source["raw"], root, shape=[512, 512, 1]) == expected
    actual_rgb = readback["rendered_rgb"]
    assert actual_rgb["shape"] == [512, 512, 3] and actual_rgb["dtype"] == "uint8"
    assert "pixels" not in actual_rgb and "no GPU readback" in actual_rgb["observation_scope"]
    rgb = actual_rgb["raw"]
    path = Path(rgb["path"])
    if not path.is_absolute():
        path = root / path
    path = path.resolve(strict=True)
    assert path.is_relative_to(root.resolve(strict=True)) and path == member_path(root, rgb["member_id"])
    assert rgb["encoding"] == "UInt8" and rgb["dtype"] == "uint8"
    assert rgb["shape"] == [512, 512, 3]
    assert rgb["sample_count"] == rgb["byte_count"] == SAMPLES * 3
    data = path.read_bytes()
    assert len(data) == rgb["byte_count"] and hashlib.sha256(data).hexdigest() == rgb["sha256"]
    expected_rgb = bytearray()
    for sample, in struct.iter_unpack("<f", expected):
        grey = math.floor(min(255., max(0., sample * (255. / 528.))) + .5)
        expected_rgb.extend([grey, grey, grey])
    assert data == bytes(expected_rgb), "Actual CPU upload RGB differs from independent live-range mapping"


def contains(value, key, expected):
    if type(value) is dict:
        return value.get(key) == expected or any(contains(child, key, expected) for child in value.values())
    if type(value) is list:
        return any(contains(child, key, expected) for child in value)
    return False


def seeded_source(value):
    assert value["dirty"] is False and value["undo_steps"] == value["redo_steps"] == 0
    assert value["workflow"] == [] and value["results"] == [] and value["selection"] is None
    assert value["previous_selection"] is None and value["spectrum_present"] is False
    assert value["recording"] is False
    calibration = value["calibration"]
    assert calibration == {"spacing": [1., 1., 1.], "origin": [0., 0., 0.],
                           "direction": [1., 0., 0., 0., 1., 0., 0., 0., 1.],
                           "unit": "px", "frame_interval": 1., "time_origin": 0., "time_unit": "s"}
    assert contains(value["metadata"], "title", "Owned R16 source")
    assert contains(value["metadata"], "r16_property", "preserve")
    assert contains(value["metadata"], "r16_extension", "unknown-kept")
    assert len(value["rois"]) == 1 and value["rois"][0]["name"] == "retained-r16-roi"
    assert contains(value["rois"], "r16_unknown", "roi-attribute-kept")
    assert contains(value["rois"], "r16_shape_unknown", "shape-attribute-kept")
    assert len(value["overlays"]) == 1
    assert contains(value["overlays"], "r16_unknown", "overlay-attribute-kept")
    assert value["display"]["properties"]["display_range"] == [0., 528.]
    assert value["display"]["crosshair"] == [257, 193, 0]
    for key in ["source_image_address", "source_buffer_address"]:
        assert type(value[key]) is str and int(value[key], 16) > 0


def actual_control(rendered, selector):
    if selector.startswith("gaussian."):
        name = selector.removeprefix("gaussian.")
        value = dict(rendered["gaussian_panel"]["control_states"][name])
        value["rect"] = rendered["gaussian_panel"]["control_rects"][name]
        value["point"] = rendered["gaussian_panel"]["controls"][name]
        return value
    if selector == "preferences.memory":
        return rendered["preferences_panel"]["fields"]["processing_memory_mb"]
    if selector.startswith("preferences."):
        return rendered["preferences_panel"]["widget_controls"][selector.removeprefix("preferences.")]
    return rendered["host_controls"][selector]


def physical_frames(row):
    frames = row["frames"]
    assert frames and [item["ordinal"] for item in frames] == list(range(1, len(frames)+1))
    sessions = []
    for frame in frames:
        usage(frame["usage"])
        rendered = frame["rendered"]
        session = rendered["session"]
        sessions.append(session)
        assert session["open"] is True
        integer(session["session_id"], positive=True)
        integer(session["context_id"], positive=True)
        assert session["current_context_id"] == session["context_id"]
        observed = rendered["input_frame"]
        assert observed["raw_inputs"] == frame["requested_events"]
        for event in frame["requested_events"]:
            assert event["type"] in {"position", "button", "key", "keys", "text", "wheel"}
            if event["type"] == "position":
                assert event["space"] == "screen"
                assert observed["mouse_position"] == [math.floor(item) for item in event["point"]]
            elif event["type"] == "button":
                assert event["name"] == "left" and type(event["down"]) is bool
                assert observed["mouse_down"][0] is event["down"]
            elif event["type"] in {"key", "keys"}:
                atoms = event["keys"] if event["type"] == "keys" else [event]
                for atom in atoms:
                    assert type(atom["down"]) is bool
                    assert (atom["name"] in observed["keys_down"]) is atom["down"]
            elif event["type"] == "text":
                assert type(event["value"]) is str
    assert len({item["session_id"] for item in sessions}) == 1
    assert len({item["context_id"] for item in sessions}) == 1
    for before, after in zip(sessions, sessions[1:]):
        assert after["imgui_frame"] == before["imgui_frame"] + 1
        assert after["imgui_time"] > before["imgui_time"]
    for observed in row["control_observations"]:
        actual = observed["control"]
        control(actual)
        assert actual["enabled"] is True
        x, y = observed["position"]
        x0, y0, x1, y1 = actual["rect"]
        assert x0 < x < x1 and y0 < y < y1
        assert x == math.floor(x) and y == math.floor(y)
        previous = integer(observed["before_frame_ordinal"], positive=True)
        assert previous < len(frames)
        assert actual == actual_control(frames[previous-1]["rendered"], observed["selector"])
        injected = frames[previous:previous+3]
        assert injected[0]["requested_events"] == [{"type": "position", "space": "screen", "point": [x, y]}]
        assert injected[1]["requested_events"] == [{"type": "button", "name": "left", "down": True}]
        assert injected[2]["requested_events"] == [{"type": "button", "name": "left", "down": False}]
    return frames


def text_edit(frames, prefix, expected, field, *, occurrence=0, count=1):
    def labelled(suffix):
        matches = [item for item in frames if item["label"] == prefix + suffix]
        assert len(matches) == count, f"Missing actual R16 text edit {prefix + suffix}"
        return matches[occurrence]

    for suffix, down in [(".select_all.down", True), (".select_all.up", False)]:
        assert labelled(suffix)["requested_events"] == [{"type": "keys", "keys": [
            {"name": "Ctrl", "down": down}, {"name": "A", "down": down}]}]
    inserted = labelled(".text")
    assert inserted["requested_events"] == [{"type": "text", "value": expected}]
    actual = inserted["rendered"]
    observed = actual["input_frame"]
    active = integer(observed["focus"]["active_id"], positive=True)
    assert observed["input_text"]["id"] == active
    if field == "search":
        assert actual["preferences_panel"]["search"] == expected
        editor = actual["preferences_panel"]["widget_controls"]["search"]
    elif field == "sigma":
        assert actual["gaussian_panel"]["sigma_text"] == expected
        editor = actual["gaussian_panel"]["control_states"]["sigma"]
    else:
        editor = actual["preferences_panel"]["fields"]["processing_memory_mb"]
        assert editor["name"] == "processing_memory_mb" and editor["kind"] == "integer"
    assert editor["id"] == active and editor["active"] is True and editor["enabled"] is True
    return inserted


def assert_row(row, context, root):
    assert row["schema_version"] == 1 and row["case_id"] == VARIANT_ID
    assert row["context"] == context and row["status"] == "passed"
    assert row["acceptance_claim"] is False and row["fixture_id"] == "resource_f32_512x512"
    assert row["fixture_formula"] == "Float32(((x+3*y)%251)/16 + (512 if (x,y)==(257,193) else 0))"
    assert row["comparison_scope"] == "Owned public native primitive integration; no independent upstream oracle"
    assert row["observation_scope"] == "Actual CPU texture-upload input/software cache; no GPU readback"
    assert json.loads(member_path(root, "case.json").read_text(encoding="utf-8")) == row
    prior = json.loads(member_path(root, "case-unverified.json").read_text(encoding="utf-8"))
    assert prior["status"] == "unverified"
    assert {key: value for key, value in prior.items() if key != "status"} == {
        key: value for key, value in row.items() if key != "status"}

    original = fixture_bytes()
    source(row["before"], root, original)
    seeded_source(row["before"])
    primitive = raw(row["owned_primitive_raw"], root)
    assert primitive != original, "R16 requires genuinely changed retained pixels"
    assert row["api_inputs"] == [{"purpose": "fixture/default preparation only",
        "methods": ["addNativeDocument", "setNativeDisplay", "mark_saved"],
        "recording": False, "tested_changes_use_api_setters": False},
        {"purpose": "initial Preferences preparation only", "method": "updateSettings",
         "values": {"processing_memory_mb": 512}, "persist": True},
        {"purpose": "bounded final/failure cleanup only",
         "methods": ["Window.cancel", "action(wait_jobs,timeout_ms=5000)", "Window.poll"]}]

    frames = physical_frames(row)
    for frame in frames:
        assert json.loads(member_path(root, f"frame-{frame['ordinal']}.json").read_text(encoding="utf-8")) == frame
    selectors = [item["selector"] for item in row["control_observations"]]
    expected_counts = {"menu.Process": 1, "menu.Process.Filters": 1,
        "menu.command.gaussian_blur_imagej": 1, "gaussian.sigma": 1,
        "gaussian.preview": 3, "gaussian.cancel": 1, "menu.Edit": 2,
        "menu.Edit.Preferences": 2, "preferences.search": 2,
        "preferences.memory": 2, "preferences.apply": 2, "preferences.close": 2}
    assert set(selectors) == set(expected_counts)
    assert {name: selectors.count(name) for name in expected_counts} == expected_counts
    text_edit(frames, "gaussian.sigma", "2", "sigma")
    for occurrence in range(2):
        text_edit(frames, "preferences.search", "processing_memory_mb", "search", occurrence=occurrence, count=2)
        text_edit(frames, "preferences.memory", str(row["derived_memory_mb"]), "memory", occurrence=occurrence, count=2)
    settled_fields = [frame for frame in frames if frame["label"] == "preferences.memory.text_settled"]
    assert len(settled_fields) == 2
    for frame in settled_fields:
        field = frame["rendered"]["preferences_panel"]["fields"]["processing_memory_mb"]
        assert field["draft"] == row["derived_memory_mb"]

    names = ["baseline", "retained_lease_ready", "lower_limit_rejected", "lease_released",
             "lower_limit_accepted", "admission_rejected", "cleanup"]
    assert [item["name"] for item in row["checkpoints"]] == names
    checkpoints = {item["name"]: item for item in row["checkpoints"]}
    last_ordinal = 0
    for checkpoint in row["checkpoints"]:
        assert json.loads(member_path(root, f"checkpoint-{checkpoint['name']}.json").read_text(encoding="utf-8")) == checkpoint
        ordinal = integer(checkpoint["frame_ordinal"], positive=True)
        assert ordinal > last_ordinal
        last_ordinal = ordinal
        assert checkpoint["rendered"] == frames[ordinal-1]["rendered"]
        unchanged(checkpoint["native_state"], row["before"], root, original)
        usage(checkpoint["usage"])
        preferences(checkpoint["preferences_file"])
        assert checkpoint["preferences_file"]["envelope"] == checkpoint["settings"]
        settings_mb = checkpoint["settings"]["values"]["processing_memory_mb"]
        native_limits(checkpoint["limits"], settings_mb)
        identifiers = [job["id"] for job in checkpoint["actual_native_jobs"]]
        assert len(identifiers) == len(set(identifiers))
        for job in checkpoint["actual_native_jobs"]:
            integer(job["id"], positive=True)
            assert job["document_id"] == row["before"]["document_id"]
            assert job["state"] in {"Queued", "Running", "Ready", "Publishing", "Completed", "Failed", "Canceled"}
            integer(job["resource_bytes"], positive=True)

    baseline, ready, rejected, released, accepted, denied, clean = [checkpoints[name] for name in names]
    assert baseline["settings"]["values"]["processing_memory_mb"] == 512
    usage(baseline["usage"], empty=True)
    assert baseline["actual_native_jobs"] == []
    cpu_readback(baseline["rendered"], root, original, preview=False)

    ready_usage = ready["usage"]
    assert ready_usage["running"] == ready_usage["queued"] == ready_usage["running_bytes"] == 0
    assert ready_usage["retained_bytes"] == ready_usage["reserved_bytes"] > MIB
    jobs = ready["rendered"]["gaussian_jobs"]
    storage = jobs["installed_preview_storage"]
    actual_storage(storage)
    assert storage["source_document_id"] == row["before"]["document_id"]
    assert storage["planes"][0]["buffer_address"] != row["before"]["source_buffer_address"]
    assert storage["planes"][0]["image_address"] != row["before"]["source_image_address"]
    assert ready_usage["retained_bytes"] >= storage["pixel_capacity_bytes"]
    lane = jobs["preview"]
    assert lane["owns_job"] is True and lane["job"]["state"] == "ready"
    assert lane["job"]["cancel_requested"] is False
    assert lane["job"]["document_id"] == row["before"]["document_id"]
    assert lane["job"]["resource_bytes"] == ready_usage["retained_bytes"]
    assert storage["job_id"] == lane["job"]["id"]
    assert storage["request_generation"] == jobs["active_installed_generation"] == lane["request_generation"]
    assert row["retained_native_job_id"] == storage["job_id"]
    assert row["retained_request_generation"] == storage["request_generation"]
    assert lane["command_record"]["options"] == {"sigma": 2., "scaled_units": False}
    assert lane["command_record"]["effective_sigma_xy"] == [2., 2.]
    assert lane["command_record"]["calibration"] == row["before"]["calibration"]
    assert lane["command_record"]["target"]["document_id"] == row["before"]["document_id"]
    assert ready["rendered"]["gaussian_panel"]["sigma"] == 2.
    assert ready["rendered"]["gaussian_panel"]["preview"] is True
    assert len(ready["actual_native_jobs"]) == 1
    assert ready["actual_native_jobs"][0]["id"] == storage["job_id"]
    assert ready["actual_native_jobs"][0]["state"] == "Ready"
    assert ready["actual_native_jobs"][0]["resource_bytes"] == ready_usage["retained_bytes"]
    native_usage = jobs["resource_usage"]
    assert native_usage["running_reserved_bytes"] == 0
    assert native_usage["retained_reserved_bytes"] == native_usage["reserved_bytes"] == ready_usage["retained_bytes"]
    assert native_usage["retained_preview_declared_bytes"] == lane["job"]["resource_bytes"]
    cpu_readback(ready["rendered"], root, primitive, preview=True)

    lower = integer(row["derived_memory_mb"], positive=True)
    assert lower == (ready_usage["retained_bytes"]-1) // MIB
    assert lower * MIB < ready_usage["retained_bytes"] <= (lower+1) * MIB
    assert lower < 512
    for field in ["usage", "limits", "settings", "preferences_file", "actual_native_jobs"]:
        assert rejected[field] == ready[field], f"Held-preview rejection changed {field}"
    assert rejected["rendered"]["gaussian_jobs"]["installed_preview_storage"] == storage
    assert rejected["rendered"]["gaussian_jobs"]["active_installed_generation"] == jobs["active_installed_generation"]
    assert rejected["rendered"]["ordinary_pixels"]["preview_generation"] == ready["rendered"]["ordinary_pixels"]["preview_generation"]
    assert "reservation" in rejected["rendered"]["preferences_panel"]["error"].lower()
    assert rejected["rendered"]["preferences_panel"]["fields"]["processing_memory_mb"]["draft"] == lower
    cpu_readback(rejected["rendered"], root, primitive, preview=True)

    for checkpoint in [released, accepted, denied, clean]:
        usage(checkpoint["usage"], empty=True)
        assert checkpoint["rendered"]["gaussian_jobs"]["installed_preview_storage"] is None
        assert checkpoint["rendered"]["gaussian_jobs"]["active_installed_generation"] is None
        assert checkpoint["rendered"]["gaussian_jobs"]["resource_usage"]["retained_preview_declared_bytes"] == 0
        cpu_readback(checkpoint["rendered"], root, original, preview=False)
    assert released["rendered"]["gaussian_panel"]["preview"] is False
    assert released["limits"] == ready["limits"] and released["settings"] == ready["settings"]
    assert released["preferences_file"] == ready["preferences_file"]
    assert accepted["settings"]["values"]["processing_memory_mb"] == lower
    assert accepted["limits"] == ready["limits"] | {"memory_bytes": lower * MIB}
    assert accepted["settings"] == ready["settings"] | {"values": ready["settings"]["values"] | {"processing_memory_mb": lower}}
    assert accepted["rendered"]["preferences_panel"]["error"] == ""
    assert preferences(accepted["preferences_file"]) != preferences(ready["preferences_file"])
    for field in ["actual_native_jobs", "usage", "limits", "settings", "preferences_file"]:
        assert denied[field] == accepted[field], f"Oversize preflight changed {field}"
    assert denied["rendered"]["gaussian_jobs"]["preview"]["owns_job"] is False
    assert denied["rendered"]["gaussian_jobs"]["preview"]["job"] is None
    assert denied["rendered"]["gaussian_jobs"]["preview"]["exit"] == "admission_failed"
    assert "budget" in denied["rendered"]["gaussian_panel"]["preview_error"].lower()
    assert clean["rendered"]["gaussian_panel"]["open"] is False
    assert clean["rendered"]["gaussian_panel"]["exit_reason"] == "cancel"

    assert [item["label"] for item in row["preferences_applies"]] == ["lower_limit_rejected", "lower_limit_accepted"]
    for applied in row["preferences_applies"]:
        assert json.loads(member_path(root, applied["label"] + "-immediate-apply.json").read_text(encoding="utf-8")) == applied
        corresponding = checkpoints[applied["label"]]
        assert applied["frame_ordinal"] == corresponding["frame_ordinal"]
        applied_frame = frames[applied["frame_ordinal"]-1]
        assert applied_frame["label"] == "preferences.apply.up"
        assert applied["preferences_panel"] == applied_frame["rendered"]["preferences_panel"]
        assert applied["immediate_file"] == corresponding["preferences_file"]
        assert applied["settings"] == corresponding["settings"]
        assert applied["limits"] == corresponding["limits"] and applied["usage"] == corresponding["usage"]
        preferences(applied["immediate_file"])
    saved_path = Path(accepted["preferences_file"]["path"]).resolve(strict=True)
    assert saved_path.is_relative_to(root.resolve(strict=True))
    assert saved_path.read_bytes() == preferences(accepted["preferences_file"])

    unchanged(row["after"], row["before"], root, original)
    assert raw(row["original_after_raw"], root) == original
    final = row["final_cleanup"]
    usage(final["usage"], empty=True)
    assert final["limits"] == accepted["limits"]
    assert final["observer_retained_result_aliases"] == 0
    assert all(job["state"] in {"Completed", "Failed", "Canceled"} for job in final["actual_native_jobs"])
    assert [item["label"] for item in row["lifecycle"]] == ["opened", "before_close", "closed"]
    opened, closing, closed = [item["observed"] for item in row["lifecycle"]]
    assert opened["open"] is closing["open"] is True and closed["open"] is False
    assert opened["session_id"] == closing["session_id"] == closed["session_id"]
    assert opened["context_id"] == closing["context_id"] == frames[0]["rendered"]["session"]["context_id"] > 0
    assert closed["context_id"] == closed["current_context_id"] == 0
    assert final["lifecycle"] == closed


@pytest.fixture(scope="module")
def cpp_report():
    configured = os.environ.get("IMAGEJXX_R16_CPP_REPORT")
    if not configured:
        if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
            pytest.fail("Required installed R16 report missing: IMAGEJXX_R16_CPP_REPORT")
        pytest.skip("Installed standalone R16 report is optional outside the required desktop gate")
    path = Path(configured).resolve(strict=True)
    report = json.loads(path.read_text(encoding="utf-8"))
    assert report["schema_version"] == 1 and report["context"] == "imgui_cpp"
    assert report["case_id"] == VARIANT_ID and report["acceptance_claim"] is False
    assert report["status"] == "passed" and len(report["cases"]) == 1
    row = report["cases"][0]
    assert row["case_id"] == VARIANT_ID and row["context"] == "imgui_cpp"
    return row, path.parent / "R16"


def python_row(directory):
    if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
        desktop = importlib.import_module("imagejxx._desktop")
    else:
        desktop = pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    assert Path(desktop.__file__).suffix in {".pyd", ".so"}
    editor = desktop.Editor(preferences_path=str(directory / "preferences.json"))
    return json.loads(editor._gaussian_resource_case(context="imgui_python", directory=str(directory)))


@pytest.mark.parametrize("context", CONTEXTS)
def test_installed_gaussian_retained_preview_resource_ui(context, request, tmp_path):
    if context == "imgui_cpp":
        row, root = request.getfixturevalue("cpp_report")
    else:
        root = tmp_path / "R16"
        assert not root.exists()
        row = python_row(root)
    assert_row(row, context, root)



