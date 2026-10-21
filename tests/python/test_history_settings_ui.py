"""Independent G010 actual shared-ImGui capture reader (stdlib and pytest).

Required runs set IMAGEJXX_REQUIRE_RENDER_TESTS=1 and
IMAGEJXX_HISTORY_SETTINGS_UI_CASE_ROOT. This file never imports imagejxx or a
renderer and never launches a product. Fresh native Windows are not OS relaunch.
"""

from functools import lru_cache
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct

import pytest

from actual_observation_transport import decode_actual_observations


CONTRACT = "p06h.history-settings-ui.001"
STEP = "SC.G010.history.step_limit"
BUDGET = "SC.G010.history.pixel_budget"
CONTEXTS = ("imgui_cpp", "imgui_python")
CASES = ("HU01", "HU02", "HU03", "HU04")
PARAMETERS = tuple((case, context) for case in CASES for context in CONTEXTS)
PARAMETER_IDS = tuple(case + "-" + context for case, context in PARAMETERS)
EXPECTED_COLLECTION_IDS = tuple("test_installed_history_settings[" + value + "]" for value in PARAMETER_IDS)
EXPECTED_COLLECTION_COUNT = 8
EXPECTED_ASSERTIONS_PER_CONTEXT = 69
MIB = 1048576
PLANE_BYTES = 262144
MAX_REPORT = 16 * MIB
MAX_ARCHIVE = 32 * MIB
MAX_FRAMES = 180
MAX_SECONDS = 15
FRAME_KEYS = ("session", "input_frame", "preferences_panel", "preferences_path", "window_error", "error_modal", "host_controls")
PHASES = {
    "HU01": ("default", "before_apply", "typed", "applied_existing", "changed", "before_prune", "pruned_two", "undo", "persisted", "reopened"),
    "HU02": ("default", "before_apply", "typed", "changed", "step_two", "zero_budget", "persisted", "reopened", "deduplicated"),
    "HU03": ("before_detached", "after_detached", "draft_changed", "draft_closed", "defaults", "revert", "pair_applied", "pair_reopened"),
    "HU04": ("before_validator", "validator_failed", "before_save", "save_failed", "before_adoption", "adoption_failed"),
}
SOURCE_ROLES = {
    "observation_transport": "tests/python/actual_observation_transport.py",
    "observation_transport_header": "src/desktop/actual_observation_transport.hpp",
    "producer": "src/desktop/history_settings_ui_probe.cpp",
    "producer_header": "src/desktop/history_settings_ui_probe.hpp",
    "window": "src/desktop/window.cpp",
    "headless_header": "src/desktop/headless_session_internal.hpp",
    "settings_panel": "src/desktop/settings_panel.cpp",
    "settings_panel_header": "src/desktop/settings_panel.hpp",
    "native_settings": "src/native/settings.cpp",
    "native_preferences": "src/native/preferences.cpp",
    "document": "src/native/document.cpp",
    "document_header": "include/imagejxx/document.hpp",
    "application": "src/native/application.cpp",
    "application_header": "include/imagejxx/application.hpp",
    "session": "src/native/session.cpp",
    "session_header": "include/imagejxx/session.hpp",
    "desktop_binding": "src/desktop/python.cpp",
    "python_wrapper": "src/imagejxx/ui/app.py",
    "reader": "tests/python/test_history_settings_ui.py",
    "adoption": "docs/parity/reviews/G010-history-settings-ownership-001.json",
    "commands": "docs/parity/commands.json",
}
STEP_HASH = "d74cb605d08471733acb7961277bf819b3c3004727cd80686691ebe52bf2c671"
UNDO_HASH = "7362892ab71c2298f769847331c54fa7be8c950ec7f2c885d695b3c27cb58232"
BUDGET_HASH = "53b222bdde41d9adc3c2ac63cb06d59e51f427e4128694c7503c3fb05f795e6c"
UI_EXPECTED = {
    STEP: {"preferences.typed": 3, "preferences.applied": 3, "draft_close.active_setting": 8,
           "defaults.draft_setting": 8, "revert.draft_setting": 8, "failure.file_unchanged": True,
           "failure.owner_policy_changed_count": 0, "failed_adoption.caller_history_changed_count": 0},
    BUDGET: {"preferences.typed": 1, "preferences.applied": 1, "draft_close.active_setting": 0,
             "defaults.draft_setting": 0, "revert.draft_setting": 0, "failure.file_unchanged": True,
             "failure.owner_policy_changed_count": 0, "failed_adoption.caller_history_changed_count": 0},
}


def _expected():
    result = {}
    for requirement in (STEP, BUDGET):
        memory = requirement == BUDGET
        for phase, units, effective, effect in (
            ("default", 0 if memory else 8, 0 if memory else 8, 2621440 if memory else 8),
            ("changed", 1 if memory else 3, MIB if memory else 3, MIB if memory else 3),
            ("persisted", 1 if memory else 3, MIB if memory else 3, MIB if memory else 3),
        ):
            for suffix, value in (("setting", units), ("current", units), ("effective", effective), ("effect", effect)):
                result[requirement, phase + "." + suffix] = value
        result[requirement, "persisted.reopened_setting"] = 1 if memory else 3
        result[requirement, "persisted.reopened_undo_steps"] = 0
        result[requirement, "persisted.reopened_redo_steps"] = 0
        for part in ("pixel", "metadata", "selection"):
            result[requirement, "policy." + part + "_revision_delta"] = 0
        result[requirement, "policy.dirty_changed"] = False
        result[requirement, "detached_settings.owner_effect_count"] = 0
        result[requirement, "adopted_pair.limit"] = 3
        result[requirement, "adopted_pair.pixel_budget_bytes"] = MIB
        result.update({(requirement, name): value for name, value in UI_EXPECTED[requirement].items()})
    result.update({(STEP, "pruned_two.undo_steps"): 2, (STEP, "pruned_two.undo_first_pixel"): 9,
                   (STEP, "policy.all_pixels"): STEP_HASH, (STEP, "pruned_two.undo_all_pixels"): UNDO_HASH,
                   (BUDGET, "one_mib.obsolete_buffer_count"): 4, (BUDGET, "one_mib.pixel_bytes"): MIB,
                   (BUDGET, "step_two.pixel_bytes"): 524288, (BUDGET, "zero_budget.step_cap_applies"): True,
                   (BUDGET, "policy.all_pixels"): BUDGET_HASH})
    assert len(result) == EXPECTED_ASSERTIONS_PER_CONTEXT
    return result


EXPECTED = _expected()
_PINS = {}


def _integer(value, low=0, high=2**64 - 1):
    assert type(value) is int and low <= value <= high
    return value


def _unique(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, "Duplicate JSON key: " + key
        result[key] = value
    return result


def _decode(raw):
    def invalid(value):
        raise AssertionError("Nonfinite JSON token: " + value)
    value = json.loads(raw, object_pairs_hook=_unique, parse_constant=invalid)
    pending, count = [(value, 0)], 0
    while pending:
        item, depth = pending.pop()
        count += 1
        assert depth <= 128 and count <= 2_000_000
        if type(item) is float:
            assert math.isfinite(item)
        elif type(item) is dict:
            pending.extend((child, depth + 1) for child in item.values())
        elif type(item) is list:
            pending.extend((child, depth + 1) for child in item)
    return value


def _json(path, maximum=MAX_REPORT):
    assert path.is_file()
    raw = path.read_bytes()
    assert 0 < len(raw) <= maximum
    return _decode(raw)


def _path(value):
    assert type(value) is str and value and Path(value).is_absolute()
    return Path(value).resolve(strict=True)


def _pin(value):
    path = _path(value["path"])
    size = _integer(value["bytes"], 1, 256 * MIB)
    digest = value["sha256"]
    assert type(digest) is str and re.fullmatch("[0-9a-f]{64}", digest)
    stat = path.stat()
    identity = (stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns)
    assert path.is_file() and stat.st_size == size
    key = (str(path), size, digest)
    if key not in _PINS:
        observed, count = hashlib.sha256(), 0
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(MIB), b""):
                count += len(block)
                assert count <= size
                observed.update(block)
        assert count == size and observed.hexdigest() == digest
        _PINS[key] = identity
    assert _PINS[key] == identity, "Pinned evidence changed: " + str(path)
    return path


def _recheck():
    for (name, size, _), identity in _PINS.items():
        stat = Path(name).stat()
        assert stat.st_size == size
        assert (stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns) == identity


def _source_set(sources):
    values = [{key: source[key] for key in ("role", "logical_path", "bytes", "sha256")}
              for source in sorted(sources, key=lambda item: item["role"])]
    return hashlib.sha256(json.dumps(values, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def _history_contracts(raw):
    # Only the exact parent-pinned outer legacy corpus may contain historical
    # nonfinite tokens in unrelated requirements. Every selected full contract
    # remains finite, duplicate-free and bound to its unchanged fingerprint.
    assert type(raw) is bytes and 0 < len(raw) <= MAX_REPORT
    outer = json.loads(raw, object_pairs_hook=_unique)
    expected = {
        STEP: "95d74d0bdf7e1e978c75dd780cb84eeb1f3be90fc4ce090e74fb0458fdc47fa3",
        BUDGET: "be2a91e861bb80666003dd5d2606f5beac44412f0b4d3c5abd72f52128bcefd6",
    }
    selected = []
    for contract in outer["surface_contracts"]:
        if contract["id"] not in expected:
            continue
        canonical = json.dumps({key: contract[key] for key in
                                ("id", "requirement_id", "snapshot", "contexts")},
                               sort_keys=True, separators=(",", ":"),
                               ensure_ascii=False, allow_nan=False).encode("utf-8")
        _decode(canonical)
        assert hashlib.sha256(canonical).hexdigest() == contract["sha256"] == expected[contract["id"]]
        selected.append(contract)
    assert len(selected) == 2 and {value["id"] for value in selected} == set(expected)
    return selected


@lru_cache(maxsize=1)
def _inputs():
    configured = os.environ.get("IMAGEJXX_HISTORY_SETTINGS_UI_CASE_ROOT")
    assert configured, "Required history paired8 capture root is missing"
    root = _path(configured)
    assert root.is_dir()
    provenance = _json(root / "provenance.json", 2 * MIB)
    assert provenance["schema_version"] == 1 and provenance["kind"] == "p06h.history-settings-ui-reader-inputs.001"
    assert set(provenance["contexts"]) == set(CONTEXTS)
    sources = provenance["sources"]
    assert type(sources) is list and len(SOURCE_ROLES) <= len(sources) <= 128
    roles = {}
    for source in sources:
        role = source["role"]
        assert type(role) is str and role and role not in roles
        _pin(source)
        if role in SOURCE_ROLES:
            assert source["logical_path"].replace("\\", "/") == SOURCE_ROLES[role]
        roles[role] = source
    assert set(SOURCE_ROLES) <= set(roles)
    reader = Path(__file__).resolve()
    assert reader.stat().st_size == roles["reader"]["bytes"]
    assert hashlib.sha256(reader.read_bytes()).hexdigest() == roles["reader"]["sha256"]
    source_set = _source_set(sources)
    assert provenance["source_set_sha256"] == source_set
    contracts = _history_contracts(_pin(roles["commands"]).read_bytes())
    for context in CONTEXTS:
        actual = {}
        for contract in contracts:
            if contract["id"] not in (STEP, BUDGET):
                continue
            for clause in contract["contexts"][context]:
                key = (contract["id"], clause["id"])
                assert key not in actual
                actual[key] = clause["expected"]
                assert clause["rule"] == ("sha256_equal" if key[1].endswith("all_pixels") else "scalar_equal")
        assert actual == EXPECTED and all(type(actual[key]) is type(EXPECTED[key]) for key in EXPECTED)
    descriptors = provenance["cases"]
    assert [(item["id"], item["context"]) for item in descriptors] == list(PARAMETERS)
    total = 0
    for descriptor in descriptors:
        raw = _pin(descriptor["raw"])
        assert raw == (root / descriptor["context"] / descriptor["id"] / "run" / "report.json").resolve(strict=True)
        assert descriptor["raw"]["bytes"] <= MAX_REPORT
        total += descriptor["raw"]["bytes"]
        if descriptor["context"] == "imgui_python":
            _pin(descriptor["launcher_origin"])
            total += descriptor["launcher_origin"]["bytes"]
    assert total <= MAX_ARCHIVE
    for context in CONTEXTS:
        binding = provenance["contexts"][context]
        installed = _path(binding["installed_root"])
        assert installed.is_dir()
        for role in ("executable", "producer_artifact", "native_artifact"):
            product = _pin(binding[role])
            if context == "imgui_cpp" or role != "executable":
                assert product == installed or installed in product.parents
        receipt = _json(_pin(binding["build_receipt"]), 2 * MIB)
        assert receipt["schema_version"] == 1 and receipt["kind"] == "p06h.history-settings-source-product-binding.001"
        assert receipt["source_set_sha256"] == binding["source_set_sha256"] == source_set
        assert receipt["sources"] == sources
        for role in ("executable", "producer_artifact", "native_artifact"):
            assert receipt["products"][context][role] == binding[role]
        assert type(binding["elapsed_seconds"]) in (int, float) and 0 < binding["elapsed_seconds"] <= 60
    return root, provenance


def _load(case, context):
    root, provenance = _inputs()
    descriptor = next(item for item in provenance["cases"] if (item["id"], item["context"]) == (case, context))
    row = decode_actual_observations(_json(_pin(descriptor["raw"])))
    assert row["schema"] == "imagejxx.history-settings-ui-observations" and row["version"] == 1
    assert row["contract"] == CONTRACT and (row["case_id"], row["context"]) == (case, context)
    assert row["observation_state"] == "observed" and row["acceptance_claim"] is False
    assert not {"expected", "PASS", "pass", "passed", "outcome"}.intersection(row)
    assert _path(row["raw_report_path"]) == _pin(descriptor["raw"])
    assert type(row["elapsed_seconds"]) in (int, float) and 0 < row["elapsed_seconds"] <= MAX_SECONDS
    runtime = row["runtime_before"]
    assert runtime == row["runtime_after"]
    _integer(runtime["process_id"], 1)
    assert runtime["pointer_bits"] == 64 and type(runtime["imgui"]) is str and runtime["imgui"]
    assert runtime["producer_source"].replace("\\", "/").endswith("/" + SOURCE_ROLES["producer"])
    binding = provenance["contexts"][context]
    for role in ("executable", "producer_artifact", "native_artifact"):
        assert runtime[role] == {key: binding[role][key] for key in ("path", "bytes", "sha256")}
        _pin(runtime[role])
    if context == "imgui_cpp":
        assert runtime["executable"] == runtime["producer_artifact"]
    else:
        origin = _json(_pin(descriptor["launcher_origin"]), 128 * 1024)
        assert origin["schema_version"] == 1 and origin["kind"] == "p06h.history-settings-python-launcher-origin.001"
        assert (origin["case_id"], origin["context"], origin["process_id"]) == (case, context, runtime["process_id"])
        assert origin["before"] == origin["after"]
        for role in ("executable", "producer_artifact", "native_artifact"):
            assert origin["before"][role] == binding[role]
        assert origin["source_set_sha256"] == provenance["source_set_sha256"]
    return row


def _checkpoint(row, phase):
    matches = [value for value in row["checkpoints"] if value["phase"] == phase]
    assert len(matches) == 1
    return matches[0]


def _state(row, phase):
    return _checkpoint(row, phase)["state"]


def _frames(row, phase):
    final = _integer(_checkpoint(row, phase)["frame_index"], 0, len(row["frames"]) - 1)
    result = [frame for index, frame in enumerate(row["frames"]) if index <= final and frame["phase"] == phase]
    assert result
    return result


def _report(row, phase):
    frame = row["frames"][_checkpoint(row, phase)["frame_index"]]
    assert frame["phase"] == phase
    return frame["report"]


def _lookup(value, path):
    assert type(path) is list and path
    for key in path:
        assert type(key) in (str, int)
        value = value[key]
    return value


def _atoms(events):
    for event in events:
        if event["type"] == "key":
            yield event
        elif event["type"] == "keys":
            yield from event["keys"]


def _clicked(row, phase, suffix):
    final = _checkpoint(row, phase)["frame_index"]
    observations = [item for item in row["control_observations"] if item["phase"] == phase and
                    item["selector"].endswith(suffix) and max(item["input_frames"]) <= final]
    assert observations, "Missing actual pointer: " + phase + "/" + suffix
    for item in observations:
        observed = _integer(item["observed_frame"], 0, final)
        control = _lookup(row["frames"][observed]["report"], item["control_path"])
        _integer(control["id"], 1, 2**32 - 1)
        assert control["enabled"] is True and control.get("visible", True) is True
        rect = control["rect"]
        assert len(rect) == 4 and rect[0] < rect[2] and rect[1] < rect[3]
        down = up = False
        for index in item["input_frames"]:
            index = _integer(index, observed, final)
            frame = row["frames"][index]
            assert frame["phase"] == phase
            for event in frame["physical_inputs"]:
                if event["type"] == "button" and event["name"] == "left":
                    x, y = frame["report"]["input_frame"]["mouse_position"]
                    assert rect[0] <= x < rect[2] and rect[1] <= y < rect[3]
                    if "clip_rect" in control:
                        a, b, c, d = control["clip_rect"]
                        assert a <= x < c and b <= y < d
                    down |= event["down"] is True
                    up |= event["down"] is False
        assert down and up
    return observations


def _typed(row, phase, name, value):
    observations = _clicked(row, phase, name)
    ids = {_lookup(row["frames"][item["observed_frame"]]["report"], item["control_path"])["id"] for item in observations}
    matches = []
    for frame in _frames(row, phase):
        field = frame["report"]["preferences_panel"].get("fields", {}).get(name)
        if not field:
            continue
        assert field["name"] == name and field["kind"] == "integer"
        assert field["identity_source"] == "current_scoped_InputInt_text_label_id"
        assert field["geometry_source"] == "actual_InputInt_group_item_rect"
        assert field["rect_kind"] == "group_including_step_buttons_and_label"
        if any(event["type"] == "text" and event["value"] == str(value) for event in frame["physical_inputs"]):
            physical = frame["report"]["input_frame"]
            assert field["id"] in ids and field["active"] is True
            assert physical["focus"]["active_id"] == physical["input_text"]["id"] == field["id"]
            assert field["enabled"] is True and field["visible"] is True
            assert field["draft"] == value and type(field["draft"]) is int
            matches.append(field)
    assert matches, "Missing actual active scoped InputInt typing: " + name
    return matches[-1]["draft"]


def _file(row, phase, role="preferences"):
    matches = [item for item in _checkpoint(row, phase)["files"] if item["role"] == role]
    assert len(matches) == 1
    value = matches[0]
    assert type(value["exists"]) is bool
    if not value["exists"]:
        assert value["byte_count"] == 0 and value["hex"] == ""
        return None
    raw = bytes.fromhex(value["hex"])
    assert len(raw) == _integer(value["byte_count"], 1, MAX_REPORT)
    assert hashlib.sha256(raw).hexdigest() == value["sha256"]
    if "json" in value:
        assert _decode(raw) == value["json"]
    return raw


def _literal(document, large, iteration):
    shape = [1, 1, 1, 128, 256, 1] if large else [1, 1, 2, 6, 8, 1]
    assert document["shape"] == shape and document["dtype"] == "uint8"
    transport = document["pixels"]
    assert transport["shape"] == shape and transport["pixel_type"] == "uint8" and transport["order"] == "TCZYXS"
    declared = bytes.fromhex(transport["hex"])
    native = bytes.fromhex(transport["storage_f64le_hex"])
    expected = bytes([iteration]) * 32768 if large else bytes(
        7*z + 3*y + x + iteration for z in range(2) for y in range(6) for x in range(8))
    assert declared == expected and len(native) == len(declared) * 8
    assert all(value[0] == byte for value, byte in zip(struct.iter_unpack("<d", native), declared))
    assert document["metadata"]["properties"]["fixture_recipe"] == ("zero;fill1..10" if large else "7*z+3*y+x;add1x10")
    assert document["metadata"]["extensions"]["future"] == [True, 17]
    assert document["calibration"]["spacing"] == [2, 3, 5]
    assert document["calibration"]["origin"] == [11, -7, 4] and document["calibration"]["unit"] == "um"
    assert document["selection"]["kind"] == "rectangle" and document["selection"]["points"] == [[1, 1], [4, 4]]
    assert document["previous_selection"]["kind"] == "line" and document["previous_selection"]["points"] == [[0, 0], [2, 2]]
    assert len(document["rois"]) == 1 and document["rois"][0]["z"] == 0
    assert document["rois"][0]["attributes"]["future.style"]["rgba"] == [1, 2, 3, 255]
    assert document["overlays"] == [] and document["spectrum_present"] is False
    assert document["sample_policy"] == "finite_only"
    return declared


def _same_documents(before, after, *, history=False):
    assert before["document_ids"] == after["document_ids"] and before["facade_ids"] == after["facade_ids"]
    assert before["active_document_id"] == after["active_document_id"]
    for collection in ("documents", "retained_documents"):
        assert set(before[collection]) == set(after[collection])
        for label in before[collection]:
            left, right = before[collection][label], after[collection][label]
            keys = ("id", "shape", "dtype", "data_identity", "dirty", "revisions", "display", "pixels", "metadata", "calibration",
                    "selection", "previous_selection", "rois", "overlays", "sample_policy", "spectrum_present")
            for key in keys + (("history",) if history else ()):
                assert left[key] == right[key], (collection, label, key)
    assert before["workflow"] == after["workflow"] and before["results"] == after["results"]
    for key in ("recording", "shortcut_overrides", "instance_role", "preferences_path"):
        assert before[key] == after[key], key


def _common(row):
    assert [item["phase"] for item in row["checkpoints"]] == list(PHASES[row["case_id"]])
    assert type(row["frames"]) is list and 1 <= len(row["frames"]) <= MAX_FRAMES
    projection = row["frame_projection"]
    assert projection["schema"] == "imagejxx.history-settings-ui-frame-projection" and projection["version"] == 1
    assert projection["keys"] == list(FRAME_KEYS)
    seen = {}
    for frame in row["frames"]:
        assert frame["phase"] in PHASES[row["case_id"]] + ("fixture_setup",)
        report = frame["report"]
        assert set(report) == set(FRAME_KEYS)
        session = report["session"]
        assert session["open"] is True and session["context_id"] == session["current_context_id"] != 0
        identity = _integer(session["session_id"], 1)
        number = _integer(session["imgui_frame"], 1)
        assert number > seen.get(identity, 0)
        seen[identity] = number
        physical = report["input_frame"]
        assert physical["raw_inputs"] == frame["physical_inputs"] and len(frame["physical_inputs"]) <= 120
        assert set(physical["modifiers"]) == {"ctrl", "alt", "shift", "super"}
        assert all(type(value) is bool for value in physical["modifiers"].values())
        assert type(physical["focus"]["window"]) is str
        assert len(physical["keys_down"]) == len(set(physical["keys_down"]))
        for event in frame["physical_inputs"]:
            assert event["type"] in {"position", "button", "key", "keys", "text", "wheel"}
    for checkpoint in row["checkpoints"]:
        index = _integer(checkpoint["frame_index"], 0, len(row["frames"]) - 1)
        assert row["frames"][index]["phase"] == checkpoint["phase"]
        state = checkpoint["state"]
        assert state["document_ids"] == state["facade_ids"] and len(set(state["document_ids"])) == len(state["document_ids"]) <= 16
        assert sorted(state["document_ids"]) == sorted(value["id"] for value in state["documents"].values())
        assert state["active_document_id"] in state["document_ids"] or not state["document_ids"]
        assert type(state["busy"]) is bool and state["busy"] is False
        assert state["settings"] == state["preferences"]["values"]
        for document in list(state["documents"].values()) + list(state["retained_documents"].values()):
            _integer(document["id"], 1)
            # These observations come from actual native Document owners.
            # snapshot_locked supplies their counter, which starts at0; the
            # optional adapter-snapshot argument does not imply positivity.
            # Policy comparisons below still require exact identity retention.
            _integer(document["data_identity"])
            assert type(document["dirty"]) is bool and set(document["revisions"]) == {"pixels", "metadata", "selection", "display"}
            for value in document["revisions"].values():
                _integer(value)
            history = document["history"]
            for key in ("undo_steps", "redo_steps", "step_limit", "pixel_budget"):
                _integer(history[key])
            assert 1 <= history["step_limit"] <= 128 and history["undo_steps"] <= history["step_limit"] and history["redo_steps"] <= history["step_limit"]
            pixels = history["pixels"]
            assert pixels["exact"] is True
            for key in ("logical_bytes", "current_bytes", "retained_bytes", "obsolete_bytes", "retained_planes"):
                _integer(pixels[key])
            assert pixels["retained_bytes"] == pixels["current_bytes"] + pixels["obsolete_bytes"]
            assert pixels["logical_bytes"] == pixels["current_bytes"] == len(bytes.fromhex(document["pixels"]["hex"])) * 8
        for file in checkpoint["files"]:
            _file(row, checkpoint["phase"], file["role"])
    for row_kind in ("api_fixture_setup", "exceptions", "reopen_observations"):
        for item in row[row_kind]:
            assert item["input_kind"] == "api_fixture_setup"
    for item in row["reopen_observations"]:
        assert item["instance_kind"] == "fresh_native_window" and item["os_relaunch"] is False
        assert item["state"] == _state(row, item["phase"])
        assert item["files"] == _checkpoint(row, item["phase"])["files"]


def _record(values, requirement, name, actual):
    key = (requirement, name)
    assert key in EXPECTED and key not in values
    expected = EXPECTED[key]
    assert type(actual) is type(expected) and actual == expected, (key, actual, expected)
    values[key] = actual


def _phase_values(values, requirement, phase, state, role):
    history = state["documents"][role]["history"]
    memory = requirement == BUDGET
    setting = state["settings"]["history_memory_mb" if memory else "history_limit"]
    current = history["pixel_budget"] // MIB if memory else history["step_limit"]
    assert not memory or history["pixel_budget"] % MIB == 0
    for suffix, actual in (("setting", setting), ("current", current),
                           ("effective", history["pixel_budget"] if memory else history["step_limit"]),
                           ("effect", history["pixels"]["obsolete_bytes"] if memory else history["undo_steps"])):
        _record(values, requirement, phase + "." + suffix, actual)


def _policy_values(values, requirement, before, after, role):
    _same_documents(before, after)
    left, right = before["documents"][role], after["documents"][role]
    for part, key in (("pixel", "pixels"), ("metadata", "metadata"), ("selection", "selection")):
        _record(values, requirement, "policy." + part + "_revision_delta", right["revisions"][key] - left["revisions"][key])
    _record(values, requirement, "policy.dirty_changed", right["dirty"] != left["dirty"])


def _reopened_values(values, requirement, state):
    history = state["documents"]["reopened"]["history"]
    _record(values, requirement, "persisted.reopened_setting", state["settings"]["history_memory_mb" if requirement == BUDGET else "history_limit"])
    _record(values, requirement, "persisted.reopened_undo_steps", history["undo_steps"])
    _record(values, requirement, "persisted.reopened_redo_steps", history["redo_steps"])
    assert state["documents"]["reopened"]["dirty"] is False
    assert history["step_limit"] == (128 if requirement == BUDGET else 3)
    assert history["pixel_budget"] == (MIB if requirement == BUDGET else 0)


def _hu01(row):
    values = {}
    for phase, role in (("default", "source"), ("changed", "changed"), ("persisted", "persisted")):
        state = _state(row, phase)
        _literal(state["documents"][role], False, 10)
        assert state["documents"][role]["dirty"] is True
        assert state["documents"][role]["history"]["redo_steps"] == 0
        _phase_values(values, STEP, phase, state, role)
    before, typed, applied = (_state(row, phase) for phase in ("before_apply", "typed", "applied_existing"))
    _same_documents(before, typed, history=True)
    assert before["settings"] == typed["settings"] and before["preferences"] == typed["preferences"]
    _record(values, STEP, "preferences.typed", _typed(row, "typed", "history_limit", 3))
    _clicked(row, "applied_existing", "apply")
    _record(values, STEP, "preferences.applied", applied["settings"]["history_limit"])
    _same_documents(before, applied)
    assert applied["documents"]["source"]["history"]["undo_steps"] == 3
    before_prune, pruned = _state(row, "before_prune"), _state(row, "pruned_two")
    _policy_values(values, STEP, before_prune, pruned, "changed")
    _record(values, STEP, "pruned_two.undo_steps", pruned["documents"]["changed"]["history"]["undo_steps"])
    _record(values, STEP, "policy.all_pixels", hashlib.sha256(_literal(pruned["documents"]["changed"], False, 10)).hexdigest())
    undo = _state(row, "undo")["documents"]["changed"]
    assert undo["history"]["undo_steps"] == 1 and undo["history"]["redo_steps"] == 1
    raw = _literal(undo, False, 9)
    _record(values, STEP, "pruned_two.undo_first_pixel", raw[0])
    _record(values, STEP, "pruned_two.undo_all_pixels", hashlib.sha256(raw).hexdigest())
    _reopened_values(values, STEP, _state(row, "reopened"))
    _literal(_state(row, "reopened")["documents"]["reopened"], False, 10)
    persisted = _decode(_file(row, "persisted"))
    assert persisted["values"]["history_limit"] == 3 and persisted["values"]["history_memory_mb"] == 0
    assert len(values) == 25
    return values


def _hu02(row):
    values = {}
    for phase, role in (("default", "source"), ("changed", "source"), ("persisted", "persisted")):
        state = _state(row, phase)
        _literal(state["documents"][role], True, 10)
        assert state["documents"][role]["dirty"] is True and state["documents"][role]["history"]["redo_steps"] == 0
        _phase_values(values, BUDGET, phase, state, role)
    before, typed, changed = (_state(row, phase) for phase in ("before_apply", "typed", "changed"))
    _typed(row, "before_apply", "history_limit", 128)
    assert before["documents"]["source"]["history"]["undo_steps"] == 10
    _same_documents(before, typed, history=True)
    assert before["settings"] == typed["settings"] and before["preferences"] == typed["preferences"]
    _record(values, BUDGET, "preferences.typed", _typed(row, "typed", "history_memory_mb", 1))
    _clicked(row, "changed", "apply")
    _record(values, BUDGET, "preferences.applied", changed["settings"]["history_memory_mb"])
    _policy_values(values, BUDGET, before, changed, "source")
    source = changed["documents"]["source"]
    history = source["history"]
    assert history["undo_steps"] == 4 and history["redo_steps"] == 0
    assert history["pixels"]["current_bytes"] == PLANE_BYTES and history["pixels"]["retained_planes"] == 5
    _record(values, BUDGET, "one_mib.obsolete_buffer_count", history["pixels"]["retained_planes"] - 1)
    _record(values, BUDGET, "one_mib.pixel_bytes", history["pixels"]["obsolete_bytes"])
    _record(values, BUDGET, "policy.all_pixels", hashlib.sha256(_literal(source, True, 10)).hexdigest())
    two = _state(row, "step_two")
    _same_documents(changed, two)
    _record(values, BUDGET, "step_two.pixel_bytes", two["documents"]["source"]["history"]["pixels"]["obsolete_bytes"])
    assert two["documents"]["source"]["history"]["undo_steps"] == 2
    zero = _state(row, "zero_budget")["documents"]["source"]
    _literal(zero, True, 13)
    history = zero["history"]
    _record(values, BUDGET, "zero_budget.step_cap_applies", history["pixel_budget"] == 0 and history["step_limit"] == 2 and
            history["undo_steps"] == 2 and history["pixels"]["obsolete_bytes"] == 524288)
    _reopened_values(values, BUDGET, _state(row, "reopened"))
    _literal(_state(row, "reopened")["documents"]["reopened"], True, 10)
    dedup = _state(row, "deduplicated")["documents"]["deduplicated"]
    _literal(dedup, True, 10)
    assert dedup["history"]["undo_steps"] == 6 and dedup["history"]["pixels"]["retained_planes"] == 5
    assert dedup["history"]["pixels"]["obsolete_bytes"] == MIB and dedup["history"]["pixels"]["current_bytes"] == PLANE_BYTES
    assert _decode(_file(row, "persisted"))["values"]["history_memory_mb"] == 1
    assert len(values) == 26
    return values


def _hu03(row):
    values = {}
    before, detached = _state(row, "before_detached"), _state(row, "after_detached")
    _literal(before["documents"]["step"], False, 3)
    _literal(before["documents"]["budget"], True, 3)
    assert all(document["dirty"] is True for document in before["documents"].values())
    assert row["detached_settings"]["history_limit"] == 2 and row["detached_settings"]["history_memory_mb"] == 1
    _same_documents(before, detached, history=True)
    assert before["settings"] == detached["settings"] and before["preferences"] == detached["preferences"]
    effects = sum(before["documents"][name] != detached["documents"][name] for name in before["documents"])
    for requirement in (STEP, BUDGET):
        _record(values, requirement, "detached_settings.owner_effect_count", effects)
    for phase in ("draft_changed", "draft_closed", "defaults", "revert"):
        state = _state(row, phase)
        _same_documents(before, state, history=True)
        assert before["settings"] == state["settings"] and before["preferences"] == state["preferences"]
    _typed(row, "draft_changed", "history_limit", 3)
    _typed(row, "draft_changed", "history_memory_mb", 1)
    _clicked(row, "draft_closed", "close_button")
    assert _report(row, "draft_closed")["preferences_panel"]["open"] is False
    for phase in ("defaults", "revert"):
        _clicked(row, phase, phase)
    for requirement, name in ((STEP, "history_limit"), (BUDGET, "history_memory_mb")):
        _record(values, requirement, "draft_close.active_setting", _state(row, "draft_closed")["settings"][name])
        _record(values, requirement, "defaults.draft_setting", _report(row, "defaults")["preferences_panel"]["fields"][name]["draft"])
        _record(values, requirement, "revert.draft_setting", _report(row, "revert")["preferences_panel"]["fields"][name]["draft"])
    _typed(row, "pair_applied", "history_limit", 3)
    _typed(row, "pair_applied", "history_memory_mb", 1)
    _clicked(row, "pair_applied", "apply")
    pair = _state(row, "pair_applied")
    _same_documents(before, pair)
    assert pair["settings"]["history_limit"] == 3 and pair["settings"]["history_memory_mb"] == 1
    persisted = _decode(_file(row, "pair_applied"))
    assert persisted["values"]["history_limit"] == 3 and persisted["values"]["history_memory_mb"] == 1
    reopened = _state(row, "pair_reopened")
    assert set(reopened["documents"]) == {"fresh", "reopened_step", "reopened_budget"}
    for name, document in reopened["documents"].items():
        _literal(document, name == "reopened_budget", 0 if name == "fresh" else 10)
        history = document["history"]
        assert history["step_limit"] == 3 and history["pixel_budget"] == MIB
        assert history["undo_steps"] == history["redo_steps"] == 0 and document["dirty"] is False
    actual = reopened["documents"]["reopened_step"]["history"]
    for requirement in (STEP, BUDGET):
        _record(values, requirement, "adopted_pair.limit", actual["step_limit"])
        _record(values, requirement, "adopted_pair.pixel_budget_bytes", actual["pixel_budget"])
    assert any(item["phase"] == "pair_reopened" and item["instance_kind"] == "fresh_native_window" and
               item["os_relaunch"] is False for item in row["reopen_observations"])
    assert len(values) == 12
    return values


def _hu04(row):
    values = {}
    unchanged_files, changed_owners = [], 0
    for before_phase, after_phase in (("before_validator", "validator_failed"), ("before_save", "save_failed")):
        before, after = _state(row, before_phase), _state(row, after_phase)
        assert set(before["documents"]) == {"step", "budget"}
        assert before["workflow"] == [{"sentinel": "workflow", "value": 7}]
        assert before["results"] == [{"sentinel": "results", "value": 11}] and before["recording"] is True
        for name, document in before["documents"].items():
            _literal(document, name == "budget", 10)
            assert document["history"]["step_limit"] == 128 and document["history"]["pixel_budget"] == 0
            assert document["dirty"] is True and document["history"]["undo_steps"] == 10 and document["history"]["redo_steps"] == 0
        _typed(row, before_phase, "history_limit", 2)
        _typed(row, before_phase, "history_memory_mb", 1)
        _clicked(row, after_phase, "apply")
        _same_documents(before, after, history=True)
        assert before["settings"] == after["settings"] and before["preferences"] == after["preferences"]
        changed_owners += sum(before["documents"][name]["history"] != after["documents"][name]["history"] for name in before["documents"])
        unchanged_files.append(_file(row, before_phase) == _file(row, after_phase))
        assert _report(row, after_phase)["preferences_panel"]["error"]
    assert _file(row, "before_validator") is not None
    assert _file(row, "before_save") is None and _file(row, "save_failed") is None
    unchanged_files.append(_file(row, "before_save", "parent_blocker") == _file(row, "save_failed", "parent_blocker"))
    assert _file(row, "before_save", "parent_blocker") is not None
    before, after = _state(row, "before_adoption"), _state(row, "adoption_failed")
    assert before["documents"] == after["documents"] == {} and before["document_ids"] == after["document_ids"] == []
    assert set(before["retained_documents"]) == set(after["retained_documents"]) == {"caller"}
    _same_documents(before, after, history=True)
    assert before["settings"] == after["settings"] and before["preferences"] == after["preferences"]
    caller = before["retained_documents"]["caller"]
    _literal(caller, False, 4)
    assert caller["dirty"] is True
    assert caller["history"]["undo_steps"] == 4 and caller["history"]["redo_steps"] == 1
    assert caller["history"]["step_limit"] == 8 and caller["history"]["pixel_budget"] == 0 and caller["revisions"]["display"] == 0
    assert before["settings"]["history_limit"] == 2 and before["settings"]["history_memory_mb"] == 1
    assert before["settings"]["default_zoom"] == 4
    assert any(item["phase"] == "adoption_failed" and item["input_kind"] == "api_fixture_setup" and
               item["operation"] == "Window.addDocument" and item["message"] for item in row["exceptions"])
    activity = row["validator_activity"]
    assert [item["phase"] for item in activity] == ["validator_failed", "save_failed", "adoption_failed"]
    for item in activity[:2]:
        _integer(item["first_calls"], 1)
        _integer(item["second_calls"], 1)
    _integer(activity[2]["zoom_refusals"], 1)
    caller_changed = int(before["retained_documents"]["caller"]["history"] != after["retained_documents"]["caller"]["history"])
    for requirement in (STEP, BUDGET):
        _record(values, requirement, "failure.file_unchanged", all(unchanged_files))
        _record(values, requirement, "failure.owner_policy_changed_count", changed_owners)
        _record(values, requirement, "failed_adoption.caller_history_changed_count", caller_changed)
    assert len(values) == 6
    return values


CASE_READERS = dict(zip(CASES, (_hu01, _hu02, _hu03, _hu04)))
CASE_ROW_COUNTS = {"HU01": 25, "HU02": 26, "HU03": 12, "HU04": 6}


@pytest.mark.parametrize("case_id,context", PARAMETERS, ids=PARAMETER_IDS)
def test_installed_history_settings(case_id, context):
    required = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert required in {"0", "1"}
    if required == "0" and not os.environ.get("IMAGEJXX_HISTORY_SETTINGS_UI_CASE_ROOT"):
        pytest.skip("Developer default has no paired history captures; no acceptance is established")
    row = _load(case_id, context)
    _common(row)
    actual = CASE_READERS[case_id](row)
    assert len(actual) == CASE_ROW_COUNTS[case_id]
    # Validate exact finite union without running another context or launching
    # anything; every row must have an explicit owning case in this module.
    owners = {}
    for requirement, name in EXPECTED:
        if name in {"detached_settings.owner_effect_count", "adopted_pair.limit", "adopted_pair.pixel_budget_bytes",
                    "draft_close.active_setting", "defaults.draft_setting", "revert.draft_setting"}:
            owner = "HU03"
        elif name.startswith("failure.") or name.startswith("failed_adoption."):
            owner = "HU04"
        else:
            owner = "HU01" if requirement == STEP else "HU02"
        owners[requirement, name] = owner
    assert set(actual) == {key for key, owner in owners.items() if owner == case_id}
    assert all(sum(owner == case for owner in owners.values()) == CASE_ROW_COUNTS[case] for case in CASES)
    _recheck()
