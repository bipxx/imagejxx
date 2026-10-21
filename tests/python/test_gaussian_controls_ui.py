"""Independent installed group01 assertions; excluded, unexecuted source.

Exact scientific bytes come from the pinned ImageJ capture, with original input
packed independently. Physical item/focus observations prove the menu/default
gestures. CPU upload-input bytes are not GPU readback. The no-image variant
requires real enabled ancestor menus and a drawn disabled Gaussian leaf.
"""

import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import sysconfig

import pytest


GROUP = "P02a.shared_ui.01.session-default-focus"
CONTEXTS = ("imgui_cpp", "imgui_python")
SUFFIXES = ("menu-open-cancel", "keyboard-enter-default", "no-image-disabled")
VARIANTS = tuple(f"{GROUP}.{suffix}" for suffix in SUFFIXES)
ROWS = tuple((variant, context) for variant in VARIANTS for context in CONTEXTS)
CONTRACT_SHA = "2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255"
NUMERICAL_SHA = "3b3f040e01b93642d2b0d3d84db9574f7a9ee525b3a7f29dac7ad21ea03ea6c3"
REFERENCE_SHA = "8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762"
INPUT_SHA = "f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4"
OUTPUT_SHA = "3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b"
REFERENCE_COMMIT = "62db90a269cccc1ac79f658e4602043214ed055b"
REFERENCE_ARCHIVE_SHA = "09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665"
GAUSSIAN_SOURCE_SHA = "2e5827a3e4b3aca0d2e43b9151be5cf2f3d1f69fb5d3e167bdd10d94e101b782"
ASSERTIONS = (
    ("fresh_defaults", "sigma_initial_active_select_all", "preview_initial_off",
     "unchanged_document", "remembered_unchanged", "no_gaussian_job"),
    ("fresh_defaults", "sigma_initial_active_select_all", "exact_owned_pixels",
     "changed_one_history", "preserved_nonpixel_state"),
    ("gaussian_menu_disabled_without_image", "no_gaussian_job", "no_document_created"),
)
FLAGS = ("acceptance_claim", "full61_verified", "full122_verified", "family_complete", "release_accepted")
USAGE_FIELDS = {"running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"}
MEMBERSHIP_FIELDS = ("application_document_ids", "desktop_document_ids", "active_document_id",
                     "workflow", "results", "recording")
PRESERVED = ("document_id", "shape_tcz_yxs", "dtype", "calibration", "selection",
             "previous_selection", "rois", "overlays", "spectrum_present", "metadata")
DISPLAY_PRESERVED = ("t", "c", "crosshair", "orthogonal", "properties")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, f"Duplicate JSON field: {key}"
        result[key] = value
    return result


def reject_constant(value):
    raise AssertionError(f"Nonfinite JSON constant: {value}")


def load_json(value):
    return json.loads(value, object_pairs_hook=unique_object, parse_constant=reject_constant)


def integer(value, *, positive=False):
    assert type(value) is int and value >= (1 if positive else 0)
    return value


def required():
    value = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert value in {"0", "1"}
    return value == "1"


def pinned_input(key, expected, *, hash_key):
    name = os.environ.get(key)
    if not name:
        if required():
            pytest.fail(f"Required installed group01 input missing: {key}")
        pytest.skip(f"Group01 input is optional outside its mandatory installed gate: {key}")
    if hash_key in os.environ:
        assert os.environ[hash_key] == expected
    path = Path(name).resolve(strict=True)
    assert path.is_file()
    with path.open("rb") as stream:
        assert hashlib.file_digest(stream, "sha256").hexdigest() == expected
    return path


def member_path(root, name):
    root = root.resolve(strict=True)
    assert type(name) is str and name
    path = (root / name).resolve(strict=True)
    assert path.is_relative_to(root) and path.is_file(), "Group01 member escapes its owned case directory"
    return path


def raw(member, root, shape):
    path = Path(member["path"])
    if not path.is_absolute():
        path = root / path
    path = path.resolve(strict=True)
    assert path == member_path(root, member["member_id"])
    assert member["dtype"] == "uint8" and member["encoding"] == "UInt8"
    assert member["shape"] == shape
    assert member["sample_count"] == member["byte_count"] == math.prod(shape)
    data = path.read_bytes()
    assert len(data) == member["byte_count"] and digest(data) == member["sha256"]
    return data


def independent_input():
    return bytes(((37*x + 61*y + 13*x*y + 19*(x % 3) + 23*(y % 5)) % 256
                  + (47 if x == 0 else 0) + (83 if y == 16 else 0)) % 256
                 for y in range(17) for x in range(23))


@pytest.fixture(scope="module")
def reference():
    contract_path = pinned_input("IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON", CONTRACT_SHA,
                                 hash_key="IMAGEJXX_GAUSSIAN_UI_CONTRACT_SHA256")
    numerical_path = pinned_input("IMAGEJXX_GAUSSIAN_CONTRACT_JSON", NUMERICAL_SHA,
                                  hash_key="IMAGEJXX_GAUSSIAN_CONTRACT_SHA256")
    capture_path = pinned_input("IMAGEJXX_GAUSSIAN_REFERENCE_JSONL", REFERENCE_SHA,
                                hash_key="IMAGEJXX_GAUSSIAN_REFERENCE_SHA256")
    contract = load_json(contract_path.read_bytes())
    numerical = load_json(numerical_path.read_bytes())
    assert numerical["comparison"]["expected_cases"] == 332
    assert numerical["comparison"]["expected_owned_output_policy"] == "reference-inside-write-extent/source-outside"
    groups = [item for item in contract["cases"] if item["id"] == GROUP]
    assert len(groups) == 1 and len(contract["numeric_oracles"]) == 19
    variants = groups[0]["variants"]
    assert [item["id"] for item in variants] == list(VARIANTS)
    for variant, predicates in zip(variants, ASSERTIONS):
        assert variant["required_contexts"] == list(CONTEXTS)
        assert variant["assertions"] == list(predicates)
    assert variants[0]["setup"]["input_oracle"] == variants[1]["setup"]["input_oracle"] == "u8-full-s2"
    assert variants[1]["output_oracle"] == "u8-full-s2" and variants[2]["setup"]["documents"] == []
    records = []
    count = 0
    with capture_path.open("r", encoding="utf-8") as stream:
        for line in stream:
            captured = load_json(line)
            count += 1
            if captured["id"] == "u8-full-s2":
                records.append(captured)
    assert count == 332 and len(records) == 1
    captured = records[0]
    oracle = contract["numeric_oracles"]["u8-full-s2"]
    assert captured["schema_version"] == 1 and captured["status"] == "unadopted_reference_numeric_observation"
    assert captured["raw_reference_output_unmodified"] is True and captured["source_preserved"] is True
    assert captured["full_plugin_runner_executed"] is False and captured["dialog_executed"] is False
    assert oracle["dtype"] == "U8" and oracle["shape"] == [17, 23]
    for key in ("dtype", "shape", "sigma_xy", "scalar_sigma", "scaled_units", "pixel_spacing_xy",
                "calibration_unit", "method", "roi", "input_sha256", "output_sha256"):
        assert captured[key] == oracle[key]
    assert oracle["sigma_xy"] == [2, 2] and oracle["scalar_sigma"] == 2
    assert oracle["scaled_units"] is False and oracle["calibration_unit"] == "pixels"
    assert oracle["method"] == "direct" and oracle["roi"]["is_area"] is False
    assert captured["outside_changed_pixels"] == 0 and captured["outside_changes"] == []
    assert captured["mask_restoration_applied"] is False
    upstream = captured["reference"]
    assert upstream["version"] == "1.54r" and upstream["commit"] == REFERENCE_COMMIT
    assert upstream["archive_sha256"] == REFERENCE_ARCHIVE_SHA
    assert upstream["source_members"]["ij/plugin/filter/GaussianBlur.java"] == GAUSSIAN_SOURCE_SHA
    assert upstream["threads"] == 1 and upstream["headless"] == "true"
    original = independent_input()
    output = bytes.fromhex(captured["output_raw_hex"])
    assert digest(original) == oracle["input_sha256"] == INPUT_SHA
    assert original == bytes.fromhex(captured["input_raw_hex"])
    assert digest(output) == oracle["output_sha256"] == oracle["owned_expected_output_sha256"] == OUTPUT_SHA
    assert oracle["raw_reference_full_output_equals_owned_expected"] is True
    assert oracle["raw_reference_outside_changed_pixels"] == 0
    assert len(output) == 391 and original != output
    return original, output


def usage(value, *, empty=False):
    assert set(value) == USAGE_FIELDS
    for item in value.values():
        integer(item)
    assert value["reserved_bytes"] == value["running_bytes"] + value["retained_bytes"]
    if empty:
        assert not any(value.values())


def actual_jobs(values):
    assert type(values) is list
    ids = []
    for job in values:
        ids.append(integer(job["id"], positive=True))
        integer(job["document_id"], positive=True)
        assert job["label"] == "Gaussian Apply"
        assert job["state"] in {"queued", "running", "ready", "publishing", "completed", "failed", "canceled"}
        assert type(job["error"]) is str and type(job["cancel_requested"]) is bool
        integer(job["resource_bytes"], positive=True)
        for key in ("completed", "total", "checkpoints", "stage_completed", "stage_total"):
            integer(job[key])
        assert type(job["stage"]) is str
    assert len(ids) == len(set(ids))
    return ids


def document_pixels(document, root, expected):
    integer(document["document_id"], positive=True)
    integer(document["data_identity"])  # A newly saved native Document legitimately starts at0.
    assert document["shape_tcz_yxs"] == [1, 1, 1, 17, 23, 1] and document["dtype"] == "uint8"
    assert len(document["planes"]) == 1
    plane = document["planes"][0]
    assert plane["position"] == [0, 0, 0] and raw(plane["raw"], root, [17, 23]) == expected
    for key in ("image_address", "buffer_address"):
        assert type(plane[key]) is str and int(plane[key], 16) > 0
    assert type(document["dirty"]) is bool
    integer(document["undo_steps"])
    integer(document["redo_steps"])
    assert set(document["revisions"]) == {"pixels", "metadata", "selection", "display"}
    for revision in document["revisions"].values():
        integer(revision)
    history = document["history"]
    assert history["undo_steps"] == document["undo_steps"] and history["redo_steps"] == document["redo_steps"]
    integer(history["step_limit"])
    integer(history["pixel_budget"])
    assert history["pixels"]["exact"] is True
    for key, value in history["pixels"].items():
        if key != "exact":
            integer(value)
    info = document["document_info"]
    assert info["id"] == document["document_id"] and info["dtype"] == "uint8"
    assert info["dirty"] is document["dirty"]
    assert info["undo_steps"] == document["undo_steps"] and info["redo_steps"] == document["redo_steps"]


def seeded_state(state, empty):
    assert state["workflow"] == state["results"] == [] and state["recording"] is False
    if empty:
        assert state["application_document_ids"] == state["desktop_document_ids"] == []
        assert state["active_document_id"] == 0 and state["document"] is None
        return
    document = state["document"]
    assert state["application_document_ids"] == state["desktop_document_ids"] == [document["document_id"]]
    assert state["active_document_id"] == document["document_id"]
    assert document["dirty"] is False and document["undo_steps"] == document["redo_steps"] == 0
    assert document["calibration"] == {
        "spacing": [1., 1., 1.], "origin": [0., 0., 0.],
        "direction": [1., 0., 0., 0., 1., 0., 0., 0., 1.], "unit": "pixels",
        "time_unit": "s", "frame_interval": 1., "time_origin": 0.,
    }
    assert document["selection"] is document["previous_selection"] is None
    assert document["spectrum_present"] is False
    assert document["rois"] == [{
        "name": "Group01 unselected ROI",
        "selection": {"kind": "rectangle", "points": [[3, 4], [9, 11]], "width": 1.,
                      "controls_shape_unknown": "shape-attribute-kept"},
        "attributes": {"controls_roi_unknown": "roi-attribute-kept"}, "t": None, "c": None, "z": 0,
    }]
    assert document["overlays"] == [{"kind": "point", "points": [[1., 1.]], "color": "#ffcc0000",
                                    "roi_name": "Group01 unselected ROI", "controls_overlay_unknown": "overlay-attribute-kept"}]
    assert document["metadata"] == {
        "title": "Owned Gaussian group01 source", "properties": {"controls_fixture": "owned-asymmetric-analytic-v1"},
        "extensions": {"controls_unknown": "metadata-attribute-kept"}, "record_layout": {},
    }
    display = document["display"]
    assert display["t"] == display["c"] == 0 and display["crosshair"] == [11, 8, 0]
    assert display["orthogonal"] is False and display["properties"]["display_range"] == [0., 255.]


def unchanged(state, before, root, original):
    for key in MEMBERSHIP_FIELDS:
        assert state[key] == before[key], f"Unaccepted Gaussian dialog changed native {key}"
    if before["document"] is None:
        assert state["document"] is None
        return
    document, source = state["document"], before["document"]
    document_pixels(document, root, original)
    for key, value in source.items():
        if key != "planes":
            assert document[key] == value, f"Unaccepted Gaussian dialog changed committed {key}"
    assert [(p["position"], p["image_address"], p["buffer_address"]) for p in document["planes"]] == [
        (p["position"], p["image_address"], p["buffer_address"]) for p in source["planes"]]


def preserved(state, before):
    for key in MEMBERSHIP_FIELDS:
        assert state[key] == before[key], f"Default Enter changed native application/output {key}"
    document, source = state["document"], before["document"]
    for key in PRESERVED:
        assert document[key] == source[key], f"Default Enter changed nonpixel field {key}"
    for key in DISPLAY_PRESERVED:
        assert document["display"][key] == source["display"][key]


def cpu_readback(rendered, root, expected):
    ordinary = rendered["ordinary_pixels"]
    assert ordinary["pixels_source"] == ordinary["contrast_source"] == "live"
    readback = ordinary["readback"]
    assert readback["available"] is True
    source, rgb = readback["source"], readback["rendered_rgb"]
    assert source["shape"] == [17, 23, 1] and rgb["shape"] == [17, 23, 3]
    for image in (source, rgb):
        assert image["dtype"] == "uint8" and "pixels" not in image
        assert "no GPU readback" in image["observation_scope"]
    assert raw(source["raw"], root, [17, 23, 1]) == expected
    assert raw(rgb["raw"], root, [17, 23, 3]) == bytes(value for sample in expected for value in (sample, sample, sample))


def actual_control(rendered, selector):
    if selector.startswith("gaussian."):
        name = selector.removeprefix("gaussian.")
        value = dict(rendered["gaussian_panel"]["control_states"][name])
        value["rect"] = rendered["gaussian_panel"]["control_rects"][name]
        value["point"] = rendered["gaussian_panel"]["controls"][name]
        return value
    return rendered["host_controls"][selector]


def physical_frames(row, root, suffix):
    frames = row["frames"]
    assert 0 < len(frames) <= 1200
    assert [frame["ordinal"] for frame in frames] == list(range(1, len(frames)+1))
    sessions = []
    for frame in frames:
        assert load_json(member_path(root, f"frame-{frame['ordinal']}.json").read_bytes()) == frame
        usage(frame["usage"])
        actual_jobs(frame["actual_native_jobs"])
        session = frame["rendered"]["session"]
        sessions.append(session)
        assert session["open"] is True
        integer(session["session_id"], positive=True)
        integer(session["context_id"], positive=True)
        assert session["current_context_id"] == session["context_id"]
        observed = frame["rendered"]["input_frame"]
        assert observed["raw_inputs"] == frame["requested_events"]
        assert observed["modifiers"] == {"ctrl": False, "shift": False, "alt": False, "super": False}
        for event in frame["requested_events"]:
            assert event["type"] in {"position", "button", "key"}, "Tested gesture substituted an API setter"
            if event["type"] == "position":
                assert set(event) == {"type", "space", "point"} and event["space"] == "screen"
                assert observed["mouse_position"] == [math.floor(value) for value in event["point"]]
            elif event["type"] == "button":
                assert set(event) == {"type", "name", "down"} and event["name"] == "left"
                assert type(event["down"]) is bool and observed["mouse_down"][0] is event["down"]
            else:
                assert set(event) == {"type", "name", "down"} and event["name"] == "Enter"
                assert type(event["down"]) is bool and ("Enter" in observed["keys_down"]) is event["down"]
    assert len({value["session_id"] for value in sessions}) == len({value["context_id"] for value in sessions}) == 1
    for previous, current in zip(sessions, sessions[1:]):
        assert current["imgui_frame"] == previous["imgui_frame"]+1 and current["imgui_time"] > previous["imgui_time"]
    selectors = ["menu.Process", "menu.Process.Filters", "menu.command.gaussian_blur_imagej"]
    if suffix == "menu-open-cancel":
        selectors.append("gaussian.cancel")
    assert [item["selector"] for item in row["control_observations"]] == selectors
    for observation in row["control_observations"]:
        previous = integer(observation["before_frame_ordinal"], positive=True)
        item = observation["control"]
        assert item == actual_control(frames[previous-1]["rendered"], observation["selector"])
        integer(item["id"], positive=True)
        enabled = not (suffix == "no-image-disabled" and observation["selector"] == selectors[-1])
        assert observation["expected_enabled"] is enabled and item["enabled"] is enabled
        assert type(item["active"]) is type(item["focused"]) is bool
        rect, point = item["rect"], observation["position"]
        assert len(rect) == 4 and len(point) == 2
        assert all(type(value) in {int, float} and math.isfinite(value) for value in rect)
        assert point == [math.floor(value) for value in item["point"]]
        assert rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]
        triple = frames[previous:previous+3]
        assert [frame["label"] for frame in triple] == [observation["selector"]+tail for tail in (".position", ".down", ".up")]
        assert [frame["requested_events"] for frame in triple] == [
            [{"type": "position", "space": "screen", "point": point}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}],
        ]
    def labelled(label):
        matches = [frame for frame in frames if frame["label"] == label]
        assert len(matches) == 1, f"Missing or duplicate actual frame {label}"
        return matches[0]
    for label in ("warmup-1", "warmup-2", "Process_popup_visible", "Filters_popup_visible", "after_native_cleanup"):
        assert labelled(label)["requested_events"] == []
    if suffix == "no-image-disabled":
        assert labelled("disabled_command_attempt_settled")["requested_events"] == []
    else:
        for label in ("Gaussian_initial_focus.1", "Gaussian_initial_focus.2"):
            assert labelled(label)["requested_events"] == []
        if suffix == "keyboard-enter-default":
            down, up = labelled("gaussian.Enter.down"), labelled("gaussian.Enter.up")
            assert up["ordinal"] == down["ordinal"]+1
            assert down["requested_events"] == [{"type": "key", "name": "Enter", "down": True}]
            assert up["requested_events"] == [{"type": "key", "name": "Enter", "down": False}]
            assert all(frame["requested_events"] == [] for frame in frames if frame["label"] == "gaussian_apply_terminal")
        else:
            assert labelled("Gaussian_cancel_settled")["requested_events"] == []
    return frames


def no_jobs(point):
    assert point["actual_native_jobs"] == []
    usage(point["usage"], empty=True)
    jobs = point["rendered"]["gaussian_jobs"]
    assert jobs["preview"]["job"] is jobs["apply"]["job"] is None
    assert jobs["installed_preview_storage"] is jobs["active_installed_generation"] is None


def opened_defaults(point):
    panel, observed = point["rendered"]["gaussian_panel"], point["rendered"]["input_frame"]
    assert panel["open"] is True and panel["sigma"] == 2. and panel["sigma_text"] == "2"
    assert panel["scaled_available"] is panel["scaled_units"] is panel["preview"] is False
    assert panel["remembered"] == {"sigma": 2., "scaled_units": False}
    assert "scaled" not in panel["controls"]
    sigma = panel["control_states"]["sigma"]
    integer(sigma["id"], positive=True)
    assert sigma["enabled"] is sigma["active"] is True
    assert observed["focus"]["active_id"] == observed["input_text"]["id"] == sigma["id"]
    text = observed["input_text"]
    assert text["length"] == 1
    assert {integer(text["selection_start"]), integer(text["selection_end"])} == {0, 1}
    assert integer(text["cursor"]) in {0, 1}
    assert panel["stack_prompt"] is False


def typed_record(record, before):
    assert set(record) == {"schema_version", "operation", "options", "calibration", "effective_sigma_xy", "target"}
    assert record["schema_version"] == 1 and record["operation"] == "gaussian_blur_imagej"
    assert record["options"] == {"sigma": 2., "scaled_units": False}
    assert record["calibration"] == before["calibration"] and record["effective_sigma_xy"] == [2., 2.]
    assert record["target"] == {
        "schema_version": 1, "document_id": before["document_id"],
        "source_revisions": [before["revisions"][key] for key in ("pixels", "metadata", "selection")],
        "source_shape": before["shape_tcz_yxs"], "scope": "ranges",
        "ranges": [[0, 1, 1], [0, 1, 1], [0, 1, 1]], "positions": [],
        "roi_policy": "active_selection", "execution_mode": "planes", "destination": "in_place",
    }


def assert_row(row, variant, context, root, reference):
    original, output = reference
    suffix = variant.removeprefix(GROUP + ".")
    empty, applies = suffix == "no-image-disabled", suffix == "keyboard-enter-default"
    assert row["schema_version"] == 1 and row["group_id"] == GROUP and row["case_id"] == variant
    assert row["context"] == context and row["status"] == "passed" and row["contract_sha256"] == CONTRACT_SHA
    assert all(row[key] is False for key in FLAGS)
    assert row["comparison_scope"] == "Exact pinned capture002 u8-full-s2; no tolerance or native/scipy expected output"
    assert row["observation_scope"] == "Actual native state and CPU texture-upload input/cache; no GPU readback"
    assert load_json(member_path(root, "case.json").read_bytes()) == row
    prior = load_json(member_path(root, "case-unverified.json").read_bytes())
    assert prior["status"] == "unverified" and "final_cleanup" not in prior
    for key, value in prior.items():
        if key == "status":
            continue
        if key in {"frames", "checkpoints", "lifecycle", "api_inputs"}:
            assert row[key][:len(value)] == value, f"Changed pre-assertion evidence {key}"
        else:
            assert row[key] == value
    preparation = {"purpose": "fresh empty host fixture preparation only", "methods": [], "documents": [],
                   "tested_changes_use_API_setters": False} if empty else {
                       "purpose": "fixture/source/type/default preparation only",
                       "methods": ["addNativeDocument", "setNativeDisplay", "mark_saved"],
                       "fixture_formula": "owned-asymmetric-analytic-v1", "input_oracle": "u8-full-s2",
                       "recording": False, "tested_changes_use_API_setters": False,
                   }
    assert row["api_inputs"] == [preparation, {
        "purpose": "bounded final/failure native-job cleanup only",
        "methods": ["Window.cancel", "action(wait_jobs,timeout_ms=5000)", "Window.poll"],
    }]
    if empty:
        assert row["oracle"] is row["fixture_input_raw"] is row["original_after_raw"] is None
        assert not list(root.glob("*.u8")) and not (root / "fixture-input.json").exists()
    else:
        assert raw(row["fixture_input_raw"], root, [17, 23]) == original
        assert load_json(member_path(root, "fixture-input.json").read_bytes()) == row["fixture_input_raw"]
        assert raw(row["original_after_raw"], root, [17, 23]) == original
        assert row["oracle"] == {
            "id": "u8-full-s2", "reference_capture_sha256": REFERENCE_SHA, "input_sha256": digest(original),
            "owned_expected_output_sha256": digest(output), "raw_reference_output_sha256": digest(output),
            "pinned_raw_reference_outside_changed_pixels": 0, "oracle_reference_executed_by_this_UI_driver": False,
        }
    frames = physical_frames(row, root, suffix)
    for frame in frames:
        if not applies:
            no_jobs(frame)
        else:
            jobs = frame["rendered"]["gaussian_jobs"]
            assert jobs["preview"]["job"] is jobs["active_installed_generation"] is jobs["installed_preview_storage"] is None
    middle = ["menu", "attempted"] if empty else ["opened", "applied" if applies else "closed"]
    names = ["baseline", *middle, "cleanup"]
    assert [point["name"] for point in row["checkpoints"]] == names
    points = dict(zip(names, row["checkpoints"]))
    before = row["before"]
    assert before == points["baseline"]["native_state"]
    seeded_state(before, empty)
    previous = 0
    for name in names:
        point = points[name]
        assert load_json(member_path(root, f"checkpoint-{name}.json").read_bytes()) == point
        ordinal = integer(point["frame_ordinal"], positive=True)
        assert previous < ordinal <= len(frames)
        previous = ordinal
        assert point["rendered"] == frames[ordinal-1]["rendered"]
        usage(point["usage"])
        actual_jobs(point["actual_native_jobs"])
        state = point["native_state"]
        if applies and name in {"applied", "cleanup"}:
            document_pixels(state["document"], root, output)
            preserved(state, before)
        else:
            unchanged(state, before, root, original)
            no_jobs(point)
        if not empty:
            cpu_readback(point["rendered"], root, output if applies and name in {"applied", "cleanup"} else original)
    if empty:
        for point in points.values():
            assert point["rendered"]["gaussian_panel"]["open"] is False
        leaf = points["menu"]["rendered"]["host_controls"]["menu.command.gaussian_blur_imagej"]
        integer(leaf["id"], positive=True)
        assert leaf["enabled"] is False
        unchanged(row["after"], before, root, original)
        assert "apply_command_record" not in row
    else:
        opened_defaults(points["opened"])
        if not applies:
            closed = points["closed"]["rendered"]["gaussian_panel"]
            assert closed["open"] is False and closed["remembered"] == points["opened"]["rendered"]["gaussian_panel"]["remembered"]
            unchanged(row["after"], before, root, original)
            assert "apply_command_record" not in row
        else:
            applied = points["applied"]
            document = applied["native_state"]["document"]
            for state in (applied["native_state"], points["cleanup"]["native_state"], row["after"]):
                document_pixels(state["document"], root, output)
                preserved(state, before)
                assert state["document"]["undo_steps"] == 1 and state["document"]["redo_steps"] == 0
                assert state["document"]["dirty"] is True
            assert document["data_identity"] != before["document"]["data_identity"]
            assert document["revisions"]["pixels"] > before["document"]["revisions"]["pixels"]
            typed_record(row["apply_command_record"], before["document"])
            jobs = applied["rendered"]["gaussian_jobs"]
            lane = jobs["apply"]
            assert lane["job"]["state"] == "completed" and lane["owns_job"] is False and lane["exit"] == "published"
            assert lane["published_document_id"] == lane["source_id"] == lane["job"]["document_id"] == before["document"]["document_id"]
            assert lane["request_generation"] is None and lane["command_record"] == row["apply_command_record"]
            assert lane["target"] == row["apply_command_record"]["target"]
            job_id = integer(lane["job"]["id"], positive=True)
            assert actual_jobs(applied["actual_native_jobs"]) == [job_id]
            scalar = applied["actual_native_jobs"][0]
            assert scalar["state"] == "completed" and scalar["error"] == "" and scalar["cancel_requested"] is False
            assert scalar["document_id"] == before["document"]["document_id"]
            assert jobs["preview"]["job"] is jobs["active_installed_generation"] is jobs["installed_preview_storage"] is None
            assert applied["rendered"]["gaussian_panel"]["open"] is False
            enter = next(frame for frame in frames if frame["label"] == "gaussian.Enter.down")
            assert enter["ordinal"] == points["opened"]["frame_ordinal"]+1
            assert all(frame["actual_native_jobs"] == [] for frame in frames[:enter["ordinal"]-1])
            for frame in frames:
                assert len(frame["actual_native_jobs"]) <= 1
                if frame["actual_native_jobs"]:
                    assert frame["actual_native_jobs"][0]["id"] == job_id
    lifecycle = row["lifecycle"]
    assert [entry["label"] for entry in lifecycle] == ["opened", "before_close", "closed"]
    opened, closing, closed = [entry["observed"] for entry in lifecycle]
    assert opened["open"] is closing["open"] is True and closed["open"] is False
    assert opened["session_id"] == closing["session_id"] == closed["session_id"] == frames[0]["rendered"]["session"]["session_id"]
    assert opened["context_id"] == closing["context_id"] == frames[0]["rendered"]["session"]["context_id"] > 0
    assert closed["context_id"] == closed["current_context_id"] == 0
    assert closing["imgui_frame"] == closed["imgui_frame"] == frames[-1]["rendered"]["session"]["imgui_frame"]
    final = row["final_cleanup"]
    usage(final["usage"], empty=True)
    usage(points["cleanup"]["usage"], empty=True)
    assert final["lifecycle"] == closed and final["Gaussian_renderer_owns_native_jobs"] is False
    assert final["observer_retained_result_aliases"] == 0
    assert final["actual_native_jobs"] == points["cleanup"]["actual_native_jobs"]
    cleanup_jobs = points["cleanup"]["rendered"]["gaussian_jobs"]
    assert cleanup_jobs["installed_preview_storage"] is cleanup_jobs["active_installed_generation"] is None
    assert cleanup_jobs["apply_busy"] is False


@pytest.fixture(scope="module")
def cpp_report():
    name = os.environ.get("IMAGEJXX_CONTROLS_CPP_REPORT")
    if not name:
        if required():
            pytest.fail("Required installed group01 report missing: IMAGEJXX_CONTROLS_CPP_REPORT")
        pytest.skip("Group01 C++ report is optional outside the mandatory installed desktop gate")
    path = Path(name).resolve(strict=True)
    assert path.name == "imgui-cpp.json" and path.is_file()
    report = load_json(path.read_bytes())
    assert report["schema_version"] == 1 and report["group_id"] == GROUP
    assert report["context"] == "imgui_cpp" and report["status"] == "passed"
    assert report["contract_sha256"] == CONTRACT_SHA and all(report[key] is False for key in FLAGS)
    rows = report["cases"]
    assert len(rows) == 3 and [row["case_id"] for row in rows] == list(VARIANTS)
    assert all(row["context"] == "imgui_cpp" for row in rows)
    return {row["case_id"]: row for row in rows}, path.parent


def python_row(variant, directory):
    if required():
        desktop = importlib.import_module("imagejxx._desktop")
    else:
        desktop = pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError)
    module = Path(desktop.__file__).resolve(strict=True)
    assert module.suffix in {".pyd", ".so"}
    if required():
        roots = {Path(sysconfig.get_path(key)).resolve() / "imagejxx" for key in ("platlib", "purelib")}
        assert any(module.is_relative_to(root) for root in roots), "Required group01 gate imported a checkout module"
    assert not directory.exists(), "Group01 driver needs a fresh owned directory"
    editor = desktop.Editor(preferences_path=str(directory / "preferences.json"))
    return load_json(editor._gaussian_controls_case(variant=variant, context="imgui_python", directory=str(directory)))


@pytest.mark.parametrize("variant,context", ROWS, ids=[f"{variant}-{context}" for variant, context in ROWS])
def test_installed_gaussian_controls_ui(variant, context, reference, request, tmp_path):
    if context == "imgui_cpp":
        rows, parent = request.getfixturevalue("cpp_report")
        row = rows[variant]
        root = parent / variant.removeprefix(GROUP + ".")
    else:
        root = tmp_path / variant.removeprefix(GROUP + ".")
        row = python_row(variant, root)
    assert_row(row, variant, context, root, reference)
