"""Independent K11–K14 actual shared-ImGui supplement; no product imports/launch.

Native policy dependency calls inside a UI host stay distinct from physical
K12–K14 controls. Required gates fail without exact source/product-bound raw.
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

CONTRACT = "p06h.shortcuts-ui.001"
CONTEXTS = ("imgui_cpp", "imgui_python")
CASES = ("K11", "K12", "K13", "K14")
PARAMETERS = tuple((case, context) for case in CASES for context in CONTEXTS)
PARAMETER_IDS = tuple(case + "-" + context for case, context in PARAMETERS)
EXPECTED_COLLECTION_IDS = tuple("test_installed_shortcuts_supplement[" + value + "]" for value in PARAMETER_IDS)
EXPECTED_COLLECTION_COUNT = 8
MAX_REPORT = 16*1024*1024
MAX_ARCHIVE = 32*1024*1024
MAX_FRAMES = 180
MAX_SECONDS = 15
PHASES = {
 "K11": ("before_policy", "after_policy"),
 "K12": ("before", "modifier_only", "portable_capture", "conflict", "after_reassign"),
 "K13": ("before_undo", "after_undo_down", "after_undo_held", "after_undo_release", "before_text_undo", "after_text_undo"),
 "K14": ("before_v3", "after_v3_save", "after_v3", "before_unapplied_export", "after_unapplied_export"),
}
COMMANDS = (
 "file.open","file.save","file.close","edit.undo","edit.redo","edit.copy","edit.cut","edit.paste",
 "edit.select_all","analysis.measure","app.preferences","app.jobs","view.channel_display","image.auto_contrast",
 "image.extract_plane","view.orthogonal","filter.gaussian_imagej","filter.rank_imagej",
)
FIXTURE_SHAPES = {"eager":[1,1,2,6,8,1],"scientific":[2,3,2,6,8,1]}
FRAME_KEYS = (
 "session","input_frame","shortcuts","host_controls","preferences_panel","file_browser","close_prompt",
 "dialog","operation_dialog","roi_manager","jobs_panel","channel_display_panel","gaussian_panel","rank_panel",
 "error_modal","preferences_path","window_error","ordinary_canvas","orthogonal_planes",
)
SOURCE_ROLES = {
 "observation_transport":"tests/python/actual_observation_transport.py",
 "observation_transport_header":"src/desktop/actual_observation_transport.hpp",
 "producer":"src/desktop/shortcuts_ui_probe.cpp",
 "producer_header":"src/desktop/shortcuts_ui_probe.hpp",
 "producer_policy":"src/desktop/shortcut_policy_observations.cpp",
 "producer_policy_header":"src/desktop/shortcut_policy_observations.hpp",
 "window":"src/desktop/window.cpp",
 "headless_header":"src/desktop/headless_session_internal.hpp",
 "input":"src/desktop/shortcut_input.cpp","input_header":"src/desktop/shortcut_input.hpp",
 "editor":"src/desktop/shortcut_editor.cpp","editor_header":"src/desktop/shortcut_editor.hpp",
 "settings_panel":"src/desktop/settings_panel.cpp","settings_panel_header":"src/desktop/settings_panel.hpp",
 "native_core":"src/native/shortcuts.cpp","native_header":"include/imagejxx/shortcuts.hpp",
 "native_settings":"src/native/settings.cpp","native_preferences":"src/native/preferences.cpp",
 "desktop_binding":"src/desktop/python.cpp","python_wrapper":"src/imagejxx/ui/app.py",
 "reader":"tests/python/test_shortcuts_ui_supplement.py",
 "adoption":"docs/parity/reviews/G012-shortcuts-adoption-001.json",
 "commands":"docs/parity/commands.json",
}
CONTRACT_DIGESTS = {
 "registry":"12187d16f509e75030879784c1c68d77993956a2f64dacec0cd44458e32600ed",
 "chords":"7168b98bdc3a2d391bc7edc1fc7933a5d8ec057771d76de2876e78b43c530ce9",
 "storage_conflicts":"2becbcf6a92a3bece20369a453a50d34425adf6390cf401a56ad0c97464ab3cd",
 "reservations":"8789ebf77c5a6dd4486ba80a7556c7edde3ca00d2b30a908f52466ffefc2bf13",
 "focus_dispatch":"302849f8f0bb9c6e8af2d02e0fd2f29f62246b2d57ed57211bc1e7bb1a4e200b",
 "editor_atomicity":"c981a9f558b789312203174f4096dc329583bb912d586dff47ce8d44b1439d30",
 "persistence_startup":"8d285d313e311f6e7683640f1f04d638e185ac57b6ac3510ca13fe633e19f099",
 "native_public_contract":"57c146ba026315db5b4024c82de0c58fdd7d821bad800797a4ad87c0e8abc28f",
}
BASELINE_BYTES=3895117
BASELINE_SHA="2031220bc79a5f396378b2eac76e48cfbb7e4ac55546626b3719868f5adbacb7"
_PINS = {}

def _canonical(value):
 return json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(",",":"),allow_nan=False).encode("utf-8")

def _path(value):
 assert type(value) is str and value
 low=value.replace("\\","/").lower()
 blocked=("/installed-products001/temp001","/installed-products001/cleanup001.json",
          "/sdk-integration002/consumer001","/sdk-integration003/consumer001",
          "/sdk-integration003/cleanup-owned-consumer001.json")
 assert not any(part in low for part in blocked),"Protected path refused before resolution"
 path=Path(value)
 assert path.is_absolute() and not path.is_symlink()
 return path.resolve(strict=True)


def _integer(value, low=0, high=2**64 - 1):
    assert type(value) is int and low <= value <= high
    return value

def _boolean(value):
    assert type(value) is bool
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

def _json(path, limit=MAX_REPORT):
    assert path.is_file(), "Missing required capture input: " + str(path)
    raw = path.read_bytes()
    assert 0 < len(raw) <= limit
    return _decode(raw)

def _inside(path, root):
    assert path == root or root in path.parents

def _pin(value):
    assert {"path", "bytes", "sha256"} <= set(value)
    size = _integer(value["bytes"], 1, 256 * 1024 * 1024)
    digest = value["sha256"]
    assert type(digest) is str and re.fullmatch("[0-9a-f]{64}", digest)
    path = _path(value["path"])
    stat = path.stat()
    identity = (stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns)
    assert path.is_file() and stat.st_size == size
    key = (str(path), size, digest)
    if key not in _PINS:
        observed, count = hashlib.sha256(), 0
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                observed.update(block)
                count += len(block)
                assert count <= size
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
              for source in sorted(sources, key=lambda value: value["role"])]
    return hashlib.sha256(json.dumps(values, sort_keys=True, separators=(",", ":")).encode()).hexdigest()

@lru_cache(maxsize=1)
def _inputs():
    configured = os.environ.get("IMAGEJXX_SHORTCUTS_UI_SUPPLEMENT_CASE_ROOT")
    assert configured, "Required paired supplement8 capture root is missing"
    root = _path(configured)
    assert root.is_dir()
    provenance = _json(root / "provenance.json", 2 * 1024 * 1024)
    assert provenance["schema_version"] == 1
    assert provenance["kind"] == "p06h.shortcuts-ui-supplement-reader-inputs.001"
    assert set(provenance["contexts"]) == set(CONTEXTS)
    sources = provenance["sources"]
    assert type(sources) is list and len(SOURCE_ROLES) <= len(sources) <= 128
    roles = {}
    for source in sources:
        role = source["role"]
        assert type(role) is str and role and role not in roles
        _pin(source)
        logical = source["logical_path"].replace("\\", "/")
        if role in SOURCE_ROLES:
            assert logical == SOURCE_ROLES[role]
        roles[role] = source
    assert set(SOURCE_ROLES) <= set(roles)
    reader = Path(__file__).resolve()
    assert reader.stat().st_size == roles["reader"]["bytes"]
    assert hashlib.sha256(reader.read_bytes()).hexdigest() == roles["reader"]["sha256"]
    source_set = _source_set(sources)
    assert provenance["source_set_sha256"] == source_set
    descriptors = provenance["cases"]
    assert [(row["id"], row["context"]) for row in descriptors] == list(PARAMETERS)
    total = 0
    for descriptor in descriptors:
        path = _pin(descriptor["raw"])
        assert path == (root / descriptor["context"] / descriptor["id"] / "run" / "report.json").resolve(strict=True)
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
            path = _pin(binding[role])
            if context == "imgui_cpp" or role != "executable":
                _inside(path, installed)
        build = _json(_pin(binding["build_receipt"]), 2 * 1024 * 1024)
        assert build["kind"] == "p06h.shortcuts-source-product-binding.001" and build["schema_version"] == 1
        explicit = _json(_pin(build["explicit_product_bindings"]), 2 * 1024 * 1024)
        assert explicit["schema_version"] == 1 and explicit["kind"] == "p06h.installed-ui-capture-bindings.001"
        assert explicit["kind_selection"] == "shortcuts-supplement"
        assert build["source_set_sha256"] == source_set == binding["source_set_sha256"]
        assert build["sources"] == sources
        for role in ("executable", "producer_artifact", "native_artifact"):
            assert build["products"][context][role] == binding[role]
        assert type(binding["elapsed_seconds"]) in (int, float) and 0 < binding["elapsed_seconds"] <= 60
    _contracts(roles["commands"])
    return root, provenance

def _origin(row, descriptor, provenance):
    context = row["context"]
    runtime = row["runtime_before"]
    assert runtime == row["runtime_after"]
    _integer(runtime["process_id"], 1)
    assert runtime["pointer_bits"] == 64
    assert type(runtime["imgui"]) is str and runtime["imgui"]
    assert runtime["producer_source"].replace("\\", "/").endswith("/" + SOURCE_ROLES["producer"])
    binding = provenance["contexts"][context]
    for role in ("executable", "producer_artifact", "native_artifact"):
        assert runtime[role] == {key: binding[role][key] for key in ("path", "bytes", "sha256")}
        _pin(runtime[role])
    if context == "imgui_cpp":
        assert runtime["executable"] == runtime["producer_artifact"]
    else:
        origin = _json(_pin(descriptor["launcher_origin"]), 128 * 1024)
        assert origin["schema_version"] == 1 and origin["kind"] == "p06h.shortcuts-python-launcher-origin.001"
        assert (origin["case_id"], origin["context"], origin["process_id"]) == (
            row["case_id"], context, runtime["process_id"])
        assert origin["before"] == origin["after"]
        for role in ("executable", "producer_artifact", "native_artifact"):
            assert origin["before"][role] == binding[role]
        assert origin["source_set_sha256"] == provenance["source_set_sha256"]

def _load(case, context):
    root, provenance = _inputs()
    descriptor = next(value for value in provenance["cases"] if (value["id"], value["context"]) == (case, context))
    row = decode_actual_observations(_json(_pin(descriptor["raw"])))
    assert row["schema"] == "imagejxx.shortcuts-ui-observations" and row["version"] == 1
    assert row["contract"] == CONTRACT and row["observation_state"] == "observed"
    assert row["acceptance_claim"] is False
    assert (row["case_id"], row["context"]) == (case, context)
    assert _path(row["raw_report_path"]) == _pin(descriptor["raw"])
    assert not {"expected", "PASS", "pass", "passed", "outcome"}.intersection(row)
    assert type(row["frames"]) is list and 1 <= len(row["frames"]) <= MAX_FRAMES
    assert type(row["elapsed_seconds"]) in (int, float) and 0 < row["elapsed_seconds"] <= MAX_SECONDS
    _origin(row, descriptor, provenance)
    return row

def _checkpoint(row, phase):
    matches = [value for value in row["checkpoints"] if value["phase"] == phase]
    assert len(matches) == 1, "Missing/duplicate actual checkpoint: " + phase
    return matches[0]

def _state(row, phase):
    return _checkpoint(row, phase)["state"]

def _frames(row, phase):
    # Cleanup following a checkpoint may use the same phase. It is observed but
    # cannot supply the earlier checkpoint's interaction or state evidence.
    last = _integer(_checkpoint(row, phase)["frame_index"], 0, len(row["frames"]) - 1)
    result = [value for index, value in enumerate(row["frames"])
              if value["phase"] == phase and index <= last]
    assert result, "Missing actual physical frame phase: " + phase
    return result

def _report(row, phase):
    index = _checkpoint(row, phase)["frame_index"]
    assert row["frames"][index]["phase"] == phase
    return row["frames"][index]["report"]

def _dispatches(row, phase):
    return [frame["report"]["shortcuts"]["dispatch"] for frame in _frames(row, phase)]

def _dispatch(row, phase, command):
    rows = _dispatches(row, phase)
    matching = [value for value in rows if value["command_id"] is not None]
    assert len(matching) == 1 and matching[0]["command_id"] == command
    assert matching[0]["reason"] == "dispatch" and matching[0]["callback_completed"] is True
    assert matching[0]["availability"][command] is True
    assert any(event["down_edge"] is True and event["repeat"] is False for event in matching[0]["events"])

def _no_dispatch(row, phase, reasons=None):
    for value in _dispatches(row, phase):
        assert value["command_id"] is None and value["callback_completed"] is False
        if reasons is not None:
            assert value["reason"] in reasons

def _plane(plane, dtype):
    count = _integer(plane["width"], 1, 8) * _integer(plane["height"], 1, 6)
    native = bytes.fromhex(plane["storage_f64le_hex"])
    declared = bytes.fromhex(plane["declared_hex"])
    assert len(native) == count * 8
    assert len(declared) == count * (1 if dtype == "uint8" else 2)
    values = list(struct.unpack("<" + "d" * count, native))
    typed = list(struct.unpack("<" + ("B" if dtype == "uint8" else "H") * count, declared))
    assert values == typed
    return values

def _literal(document, fixture):
    shape = FIXTURE_SHAPES[fixture]
    assert document["shape"] == shape
    dtype = "uint8" if fixture == "eager" else "uint16"
    assert document["dtype"] == dtype
    positions = [(t, c, z) for t in range(shape[0]) for c in range(shape[1]) for z in range(shape[2])]
    assert [(p["t"], p["c"], p["z"]) for p in document["planes"]] == positions
    for plane in document["planes"]:
        assert (plane["width"], plane["height"]) == (8, 6)
        t, c, z = plane["t"], plane["c"], plane["z"]
        expected = [(7*z + 3*y + x if fixture == "eager" else 40*t + 30*c + 5*z + 2*y + x)
                    for y in range(6) for x in range(8)]
        assert _plane(plane, dtype) == expected

def _bindings(state):
    profile = state["profile"]
    assert profile["registry_version"] == 1
    assert [value["command_id"] for value in profile["bindings"]] == list(COMMANDS)
    assert state["settings"]["shortcut_bindings"] == profile["overrides"]
    assert state["preferences"]["values"]["shortcut_bindings"] == profile["overrides"]
    return {value["command_id"]: value["chord"] for value in profile["bindings"]}

def _same_data(a, b, *, display=True, selection=True, history=True):
    assert a["document_ids"] == b["document_ids"] and a["facade_ids"] == b["facade_ids"]
    assert a["active_document_id"] == b["active_document_id"]
    assert set(a["documents"]) == set(b["documents"])
    keys = ["id", "data_identity", "dirty", "metadata", "calibration", "overlays",
            "planes", "shape", "dtype", "sample_policy", "spectrum_present"]
    if display:
        keys += ["display", "revisions"]
    if selection:
        keys += ["selection", "previous_selection", "rois"]
    if history:
        keys += ["history"]
    for label in a["documents"]:
        for key in keys:
            assert a["documents"][label][key] == b["documents"][label][key], (label, key)
    assert a["workflow"] == b["workflow"] and a["results"] == b["results"]

def _persisted(row, phase, role="preferences"):
    values = [value for value in _checkpoint(row, phase)["files"] if value["role"] == role]
    assert len(values) == 1 and values[0]["exists"] is True
    value = values[0]
    raw = bytes.fromhex(value["hex"])
    assert len(raw) == _integer(value["byte_count"], 0, MAX_REPORT)
    assert hashlib.sha256(raw).hexdigest() == value["sha256"]
    if "json" in value:
        assert _decode(raw) == value["json"]
    return raw

def _lookup(value, path):
    assert type(path) is list and path
    for key in path:
        assert type(key) in (str, int)
        value = value[key]
    return value

def _atoms(inputs):
    for event in inputs:
        if event["type"] == "key":
            yield event
        elif event["type"] == "keys":
            yield from event["keys"]

def _key(row, phase, key, *, ctrl=False, shift=False, alt=False, super=False, released=True):
    found_down = found_up = False
    for frame in _frames(row, phase):
        physical = frame["report"]["input_frame"]
        for event in _atoms(frame["physical_inputs"]):
            if event["name"] != key:
                continue
            if event["down"]:
                assert physical["modifiers"] == {"ctrl": ctrl, "shift": shift, "alt": alt, "super": super}
                assert key in physical["keys_down"]
                found_down = True
            else:
                found_up = True
    assert found_down and (found_up or not released), "Missing real down/up chord in " + phase + ": " + key

def _clicked(row, phase, suffix):
    observations = [value for value in row["control_observations"]
                    if value["phase"] == phase and value["selector"].endswith(suffix)]
    assert observations, "Missing actual pointer control: " + phase + "/" + suffix
    for value in observations:
        index = _integer(value["observed_frame"], 0, len(row["frames"]) - 1)
        control = _lookup(row["frames"][index]["report"], value["control_path"])
        _integer(control["id"], 1, 2**32 - 1)
        assert control["enabled"] is True
        if "visible" in control:
            assert control["visible"] is True
        rect = control["rect"]
        assert len(rect) == 4 and all(type(v) in (int, float) and math.isfinite(v) for v in rect)
        assert rect[0] < rect[2] and rect[1] < rect[3]
        point = control["point"]
        assert len(point) == 2 and rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]
        if "clip_rect" in control:
            clip = control["clip_rect"]
            assert len(clip) == 4
            assert clip[0] <= point[0] < clip[2] and clip[1] <= point[1] < clip[3]
        down = up = False
        for target in value["input_frames"]:
            target = _integer(target, index + 1, _checkpoint(row, phase)["frame_index"])
            frame = row["frames"][target]
            assert frame["phase"] == phase
            for event in frame["physical_inputs"]:
                if event["type"] == "button" and event["name"] == "left":
                    x, y = frame["report"]["input_frame"]["mouse_position"]
                    assert rect[0] <= x < rect[2] and rect[1] <= y < rect[3]
                    if "clip_rect" in control:
                        assert clip[0] <= x < clip[2] and clip[1] <= y < clip[3]
                    down |= event["down"] is True
                    up |= event["down"] is False
        assert down and up

def _source(row, phase):
    return _state(row, phase)["documents"]["source"]

def _draft(row, phase):
    return _report(row, phase)["preferences_panel"]["shortcut_editor"]["draft_override_lines"]

def _profile(row, phase, overrides):
    state = _state(row, phase)
    assert state["profile"]["overrides"] == overrides
    return _bindings(state)


def _common(row):
    expected = PHASES[row["case_id"]]
    projection = row["frame_projection"]
    assert projection["schema"] == "imagejxx.shortcuts-ui-frame-projection" and projection["version"] == 1
    assert projection["keys"] == list(FRAME_KEYS)
    assert [value["phase"] for value in row["checkpoints"]] == list(expected)
    assert type(row["api_fixture_setup"]) is list
    for setup in row["api_fixture_setup"]:
        assert setup["input_kind"] == "api_fixture_setup"
        assert type(setup["operation"]) is str and setup["operation"]
    last_frame = {}
    for frame in row["frames"]:
        assert frame["phase"] in expected or frame["phase"] == "fixture_setup"
        assert type(frame["physical_inputs"]) is list and len(frame["physical_inputs"]) <= 120
        report = frame["report"]
        assert set(report) <= set(FRAME_KEYS)
        session = report["session"]
        assert session["open"] is True
        session_id = _integer(session["session_id"], 1)
        assert session["context_id"] == session["current_context_id"] and session["context_id"] != 0
        number = _integer(session["imgui_frame"], 1)
        assert number > last_frame.get(session_id, 0)
        last_frame[session_id] = number
        physical = report["input_frame"]
        assert physical["raw_inputs"] == frame["physical_inputs"]
        assert set(physical["modifiers"]) == {"ctrl", "alt", "shift", "super"}
        for value in physical["modifiers"].values():
            _boolean(value)
        assert len(physical["keys_down"]) == len(set(physical["keys_down"]))
        assert type(physical["focus"]["window"]) is str
        for event in frame["physical_inputs"]:
            assert event["type"] in {"position", "button", "key", "keys", "text", "wheel"}
            for atom in _atoms([event]):
                assert type(atom["name"]) is str and type(atom["down"]) is bool
        dispatch = report["shortcuts"]["dispatch"]
        assert dispatch["command_id"] is None or dispatch["command_id"] in COMMANDS
        assert set(dispatch["availability"]) == set(COMMANDS)
        for value in dispatch["availability"].values():
            _boolean(value)
        if dispatch["command_id"] is not None:
            assert dispatch["reason"] == "dispatch" and dispatch["callback_completed"] is True
        for event in dispatch["events"]:
            assert event["down_edge"] is True and event["repeat"] is False
        for name in ("delta_time", "key_repeat_delay", "key_repeat_rate"):
            assert type(frame[name]) in (int, float) and math.isfinite(frame[name]) and frame[name] > 0
    for phase in expected:
        checkpoint = _checkpoint(row, phase)
        frame_index = _integer(checkpoint["frame_index"], 0, len(row["frames"]) - 1)
        assert row["frames"][frame_index]["phase"] == phase
        state = checkpoint["state"]
        assert type(state["documents"]) is dict and len(state["documents"]) <= 16
        assert len(state["document_ids"]) == len(set(state["document_ids"])) <= 16
        assert state["document_ids"] == state["facade_ids"]
        assert sorted(state["document_ids"]) == sorted(value["id"] for value in state["documents"].values())
        assert state["active_document_id"] in state["document_ids"] or not state["document_ids"]
        _bindings(state)
        _boolean(state["busy"])
        for document in state["documents"].values():
            _integer(document["id"], 1)
            # Native Document identities start at zero, including clean session
            # reopen. These are real Document snapshots, so absent adapter-only
            # optional identities are still refused by the strict integer check.
            _integer(document["data_identity"])
            _boolean(document["dirty"])
            assert set(document["revisions"]) == {"pixels", "metadata", "selection", "display"}
            for value in document["revisions"].values():
                _integer(value)
            for name in ("undo_steps", "redo_steps", "step_limit", "pixel_budget"):
                _integer(document["history"][name])
        for file in checkpoint["files"]:
            _boolean(file["exists"])
            if file["exists"]:
                raw = bytes.fromhex(file["hex"])
                assert len(raw) == file["byte_count"]
                assert hashlib.sha256(raw).hexdigest() == file["sha256"]
                if "json" in file:
                    assert _decode(raw) == file["json"]
    for exception in row["exceptions"]:
        assert exception["input_kind"] == "api_fixture_setup"
        assert exception["phase"] in expected
        assert exception["type"] and exception["message"]


def _contracts(identity):
    """Only the exact historical corpus may retain unrelated Infinity tokens."""
    raw = _pin(identity).read_bytes()
    assert len(raw) == BASELINE_BYTES and hashlib.sha256(raw).hexdigest() == BASELINE_SHA
    outer = json.loads(raw, object_pairs_hook=_unique)
    selected = {}
    for family, digest in CONTRACT_DIGESTS.items():
        requirement = "G012.shortcuts." + family
        rows = [value for value in outer["surface_contracts"] if value["requirement_id"] == requirement]
        assert len(rows) == 1
        contract = _decode(_canonical(rows[0]))
        assert contract["id"] == "SC." + requirement and contract["sha256"] == digest
        assert hashlib.sha256(_canonical({key: contract[key] for key in (
            "id", "requirement_id", "snapshot", "contexts")})).hexdigest() == digest
        assert contract["contexts"]["cpp_api"] == contract["contexts"]["python_api"]
        selected[requirement] = contract
    assert sum(len(value["contexts"]["cpp_api"]) for value in selected.values()) == 67
    return selected


def _rules(context):
    _, provenance = _inputs()
    identity = next(value for value in provenance["sources"] if value["role"] == "commands")
    return {(requirement, rule["id"]): rule for requirement, contract in _contracts(identity).items()
            for rule in contract["contexts"][context]}


def _value(requirement, name, actual, *, context):
    rule = _rules(context)[requirement, name]
    kind = "sha256" if rule["rule"] == "sha256_equal" else rule["value_type"]
    types = {"boolean": type(actual) is bool, "integer": type(actual) is int,
             "number": type(actual) in (int, float), "string": type(actual) is str,
             "null": actual is None, "sha256": type(actual) is str and
             re.fullmatch("[0-9a-f]{64}", actual) is not None}
    assert kind in types and types[kind], "Wrong observed type: " + requirement + "/" + name
    assert _canonical(actual) == _canonical(rule["expected"]), "Actual mismatch: " + requirement + "/" + name
    return {"requirement_id": requirement, "id": name, "value": actual}


def _extra(row, family, name, actual):
    return _value("G012.shortcuts." + family, name, actual, context=row["context"])


def _editor(row, phase):
    return _report(row, phase)["preferences_panel"]["shortcut_editor"]


def _editor_row(row, phase, command):
    rows = [value for value in _editor(row, phase)["rows"] if value["command_id"] == command]
    assert len(rows) == 1
    return rows[0]


def _completed(row, phase, command=None):
    return [value for value in _dispatches(row, phase)
            if value["callback_completed"] is True and
            (command is None or value["command_id"] == command)]


def _counts(value):
    assert set(value) == {"plane_reads", "region_reads", "overflowed"}
    return (_integer(value["plane_reads"]), _integer(value["region_reads"]), _boolean(value["overflowed"]))


def _k11(row):
    native = row["native_policy_observations"]
    assert native["scope"] == "native_policy_executed_in_ui_host"
    assert set(native) == {"scope", "observations", "auxiliary"}
    rules = _rules("cpp_api")
    values = {}
    for observation in native["observations"]:
        assert set(observation) == {"requirement_id", "id", "value"}
        key = observation["requirement_id"], observation["id"]
        assert key in rules and key not in values
        actual = observation["value"]
        if rules[key]["rule"] == "sha256_equal":
            # Registry containers are native actual values, never producer hashes.
            assert type(actual) in (dict, list)
            actual = hashlib.sha256(_canonical(actual)).hexdigest()
        values[key] = _value(*key, actual, context="cpp_api")
    assert set(values) == set(rules) and len(values) == 67
    auxiliary = native["auxiliary"]
    positive = auxiliary["read_observer_positive_control"]
    _integer(positive["document_id"], 1)
    assert _counts(positive["before"]) == (0, 0, False)
    assert _counts(positive["after_plane"]) == (1, 0, False)
    assert _counts(positive["after_region"]) == (1, 1, False)
    assert positive["plane_samples"] == [0, 1, 2, 3, 4, 5]
    assert positive["region_samples"] == [1, 2, 4, 5]
    owner = auxiliary["native_no_io_owner"]
    _integer(owner["document_id"], 1)
    assert owner["revisions_after"] == owner["revisions_before"]
    assert _counts(owner["reads_before"]) == _counts(owner["reads_after"]) == (0, 0, False)
    assert owner["shares_data"] is True and owner["dirty_before"] == owner["dirty_after"]
    assert owner["conflict_diagnostics"]
    assert values["G012.shortcuts.native_public_contract", "native.document_revision_delta"]["value"] == sum(
        owner["revisions_after"][name] - before for name, before in owner["revisions_before"].items())
    window = auxiliary["window_configuration"]
    assert window["recording"] is True and window["workflow_before"] == window["workflow_after"]
    assert window["workflow_before"] and window["workflow_before"][-1]["operation"] == "add"
    assert window["workflow_before"][-1]["parameters"]["value"] == 1
    assert window["before_revisions"]["pixels"] > window["initial_revisions"]["pixels"]
    assert window["before_revisions"] == window["after_revisions"]
    assert window["profile_after"] == ["analysis.measure=Primary+K"]
    assert window["measure_chord_after"] == "Ctrl+K" and window["restored_profile"] == []
    assert window["settings_before"]["shortcut_bindings"] == []
    assert window["settings_after"]["shortcut_bindings"] == ["analysis.measure=Primary+K"]
    assert window["conflict_diagnostics"]
    state = _state(row, "after_policy")
    assert _bindings(state)["analysis.measure"] == "M" and state["profile"]["overrides"] == []
    assert state["documents"]["source"]["id"] == window["native_document_id"]
    assert state["documents"]["source"]["revisions"] == window["after_revisions"]
    assert state["workflow"] == window["workflow_after"]
    preferences = auxiliary["preferences"]
    saved = _decode(preferences["saved_utf8"])
    assert saved["values"]["shortcut_bindings"] == preferences["reopened_override_lines"]
    assert "future.tool=opaque future v1" in preferences["reopened_override_lines"]
    assert preferences["recovery_warnings"] and preferences["failed_import_refused"] is True
    assert preferences["failed_import_before_utf8"] == preferences["failed_import_after_utf8"]
    assert len(auxiliary["owned_files"]) == 3
    for identity in auxiliary["owned_files"]:
        _pin(identity)
    cap = auxiliary["native_working_cap"]
    assert cap["native_exception_category"] == "length_error" and cap["message"]
    assert type(cap["max_native_working_bytes"]) is int and cap["max_native_working_bytes"] > 0
    return list(values.values())


def _k12(row):
    before = _state(row, "before")
    _clicked(row, "modifier_only", "capture")
    modifier_frames = _frames(row, "modifier_only")
    assert any(event["name"] == "Ctrl" and event["down"] is True
               for frame in modifier_frames for event in _atoms(frame["physical_inputs"]))
    modifier = _editor(row, "modifier_only")
    assert modifier["capture_state"] == "armed" and modifier["capture_active"] is True
    assert modifier["candidate"] == "" and modifier["candidate_valid"] is False
    assert _report(row, "modifier_only")["input_frame"]["modifiers"]["ctrl"] is True
    assert not any(event["down"] is True and event["name"] not in {"Ctrl", "Alt", "Shift", "Super"}
                   for frame in modifier_frames for event in _atoms(frame["physical_inputs"]))
    _key(row, "portable_capture", "LeftBracket", ctrl=True, shift=True)
    portable = _editor(row, "portable_capture")
    assert portable["portable_primary"] is True and portable["candidate_valid"] is True
    assert portable["capture_state"] == "idle" and portable["candidate_physical"] == "Ctrl+Shift+LeftBracket"
    _key(row, "conflict", "S", ctrl=True)
    conflict = _editor(row, "conflict")
    assert conflict["candidate"] == "Primary+S" and conflict["candidate_valid"] is False
    diagnostics = [value for value in conflict["candidate_diagnostics"] if value["kind"] == "conflict"]
    assert len(diagnostics) == 1
    ids = sorted([diagnostics[0]["command_id"], diagnostics[0]["other_command_id"]])
    assert set(diagnostics[0]["platforms"]) == {"Windows", "Linux", "macOS"}
    apply = _report(row, "conflict")["preferences_panel"]["widget_controls"]["apply"]
    _integer(apply["id"], 1)
    assert len(apply["rect"]) == 4 and apply["rect"][0] < apply["rect"][2] and apply["rect"][1] < apply["rect"][3]
    assert conflict["controls"]["assign"]["enabled"] is False
    assert conflict["controls"]["reassign_conflict"]["enabled"] is True
    _clicked(row, "after_reassign", "reassign_conflict")
    reassigned = _editor(row, "after_reassign")
    assert reassigned["draft_override_lines"] == ["file.save=", "analysis.measure=Primary+S"]
    assert reassigned["candidate"] == "" and reassigned["candidate_diagnostics"] == []
    assert _report(row, "after_reassign")["preferences_panel"]["widget_controls"]["apply"]["enabled"] is True
    for phase in PHASES["K12"]:
        _same_data(before, _state(row, phase))
        assert _bindings(_state(row, phase))["analysis.measure"] == "M"
        assert _state(row, phase)["profile"]["overrides"] == []
        assert not _completed(row, phase)
    return [
        _extra(row, "chords", "capture.portable.canonical", portable["candidate"]),
        _extra(row, "chords", "capture.modifier_only.assign_enabled", modifier["controls"]["assign"]["enabled"]),
        _extra(row, "storage_conflicts", "conflict.dialog.command_ids", "|".join(ids)),
        _extra(row, "storage_conflicts", "conflict.apply_enabled", apply["enabled"]),
        _extra(row, "storage_conflicts", "reassign.victim.chord", _editor_row(row, "after_reassign", "file.save")["draft"]),
        _extra(row, "storage_conflicts", "reassign.target.windows_chord", _editor_row(row, "after_reassign", "analysis.measure")["draft"]),
    ]


def _k13(row):
    before = _source(row, "before_undo")
    assert before["history"]["undo_steps"] == 2 and before["history"]["redo_steps"] == 0
    _key(row, "after_undo_down", "Z", ctrl=True, released=False)
    dispatched = _completed(row, "after_undo_down", "edit.undo")
    after = _source(row, "after_undo_down")
    assert after["history"]["undo_steps"] == 1 and after["history"]["redo_steps"] == 1
    assert after["data_identity"] != before["data_identity"]
    assert after["revisions"]["pixels"] == before["revisions"]["pixels"]
    held = _frames(row, "after_undo_held")
    assert len(held) >= 24
    assert sum(frame["delta_time"] for frame in held) > max(frame["key_repeat_delay"] for frame in held)
    assert all({"Ctrl", "Z"} <= set(frame["report"]["input_frame"]["keys_down"]) for frame in held)
    assert not any(event["down"] is True and event["name"] in {"Ctrl", "Z"}
                   for frame in held for event in _atoms(frame["physical_inputs"]))
    _same_data(_state(row, "after_undo_down"), _state(row, "after_undo_held"))
    assert not _completed(row, "after_undo_release")
    release = _frames(row, "after_undo_release")
    for name in ("Ctrl", "Z"):
        assert any(event["name"] == name and event["down"] is False
                   for frame in release for event in _atoms(frame["physical_inputs"]))
    _clicked(row, "before_text_undo", "search")
    text = _report(row, "before_text_undo")["input_frame"]["input_text"]
    assert text["id"] != 0 and text["length"] > 0
    assert _report(row, "before_text_undo")["preferences_panel"]["search"] == "Measure"
    _key(row, "after_text_undo", "Z", ctrl=True)
    presses = [frame for frame in _frames(row, "after_text_undo")
               if any(event["name"] == "Z" and event["down"] is True
                      for event in _atoms(frame["physical_inputs"]))]
    assert len(presses) == 1
    physical = presses[0]["report"]["input_frame"]
    assert physical["input_text"]["id"] == text["id"]
    assert presses[0]["report"]["shortcuts"]["dispatch"]["focus"] in {"text", "control"}
    _same_data(_state(row, "before_text_undo"), _state(row, "after_text_undo"))
    assert _source(row, "after_text_undo")["history"]["undo_steps"] == 1
    assert _source(row, "after_text_undo")["history"]["redo_steps"] == 1
    return [
        _extra(row, "focus_dispatch", "actual.undo.callback_count", len(dispatched)),
        _extra(row, "focus_dispatch", "held.undo.additional_callbacks", len(_completed(row, "after_undo_held", "edit.undo"))),
        _extra(row, "focus_dispatch", "text.undo.callback_count", len(_completed(row, "after_text_undo", "edit.undo"))),
    ]


def _k14(row):
    before = _state(row, "before_v3")
    assert before["profile"]["overrides"] == ["analysis.measure=Primary+K"]
    assert _bindings(before)["analysis.measure"] == "Ctrl+K"
    _key(row, "after_v3_save", "S", ctrl=True)
    _dispatch(row, "after_v3_save", "file.save")
    _clicked(row, "after_v3_save", "file.save")
    session_bytes = _persisted(row, "after_v3_save", "owned_document")
    assert session_bytes.startswith(b"IMAGEJXX3\n")
    _key(row, "after_v3", "O", ctrl=True)
    _dispatch(row, "after_v3", "file.open")
    _clicked(row, "after_v3", "file.open")
    opened = _state(row, "after_v3")
    assert len(opened["document_ids"]) == len(before["document_ids"]) + 1
    restored = opened["documents"]["opened"]
    assert restored["id"] not in before["document_ids"] and restored["data_identity"] == 0
    assert restored["history"]["undo_steps"] == restored["history"]["redo_steps"] == 0
    assert restored["dirty"] is False and all(value == 0 for value in restored["revisions"].values())
    _literal(restored, "eager")
    original = before["documents"]["source"]
    for name in ("shape", "dtype", "sample_policy", "calibration", "metadata", "planes", "selection", "previous_selection", "rois", "overlays"):
        assert restored[name] == original[name]
    assert _persisted(row, "after_v3", "owned_document") == session_bytes
    _key(row, "before_unapplied_export", "J", ctrl=True)
    _clicked(row, "before_unapplied_export", "assign")
    draft = _editor(row, "before_unapplied_export")
    assert draft["draft_override_lines"] == ["analysis.measure=Primary+J"]
    assert _editor_row(row, "before_unapplied_export", "analysis.measure")["draft"] == "Ctrl+J"
    assert _bindings(_state(row, "before_unapplied_export"))["analysis.measure"] == "Ctrl+K"
    _clicked(row, "after_unapplied_export", "export")
    exported = _decode(_persisted(row, "after_unapplied_export", "exported_preferences"))
    assert exported["format"] == "imagejxx-preferences" and exported["version"] == 1
    assert exported["values"]["shortcut_bindings"] == ["analysis.measure=Primary+K"]
    assert _editor(row, "after_unapplied_export")["draft_override_lines"] == ["analysis.measure=Primary+J"]
    for phase in PHASES["K14"]:
        assert _state(row, phase)["profile"] == before["profile"]
        assert _state(row, phase)["workflow"] == before["workflow"]
    _same_data(opened, _state(row, "after_unapplied_export"))
    changed_bindings = sum(_bindings(before)[name] != _bindings(opened)[name] for name in COMMANDS)
    unapplied_rows = set(draft["draft_override_lines"]) - set(before["profile"]["overrides"])
    exported_unapplied = sum(line in unapplied_rows for line in exported["values"]["shortcut_bindings"])
    return [
        _extra(row, "persistence_startup", "native_v3.active_profile_changed_count", changed_bindings),
        _extra(row, "persistence_startup", "export.unapplied_override_count", exported_unapplied),
    ]


CASE_READERS = dict(zip(CASES, (_k11, _k12, _k13, _k14)))
CASE_OBSERVATION_COUNTS = {"K11": 67, "K12": 6, "K13": 3, "K14": 2}


def _write_observations(row, values):
    configured = os.environ.get("IMAGEJXX_SHORTCUTS_UI_SUPPLEMENT_OBSERVATIONS")
    if not configured:
        return
    output = _path(configured)
    assert output.is_dir()
    _, provenance = _inputs()
    descriptor = next(value for value in provenance["cases"]
                      if (value["id"], value["context"]) == (row["case_id"], row["context"]))
    payload = {"schema_version": 1, "kind": "p06h.shortcuts-ui-supplement.computed-observations.001",
               "case_id": row["case_id"], "context": row["context"],
               "raw": descriptor["raw"], "source_set_sha256": provenance["source_set_sha256"],
               "observation_scope": "native_policy_dependency" if row["case_id"] == "K11" else "physical_shared_imgui",
               "observations": values}
    raw = json.dumps(payload, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False).encode("utf-8") + b"\n"
    assert len(raw) <= 256*1024
    with (output / (row["case_id"] + "." + row["context"] + ".json")).open("xb") as stream:
        stream.write(raw)


@pytest.mark.parametrize("case_id,context", PARAMETERS, ids=PARAMETER_IDS)
def test_installed_shortcuts_supplement(case_id, context):
    required = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert required in {"0", "1"}
    if required == "0" and not os.environ.get("IMAGEJXX_SHORTCUTS_UI_SUPPLEMENT_CASE_ROOT"):
        pytest.skip("Developer default has no supplement captures; no acceptance is established")
    row = _load(case_id, context)
    _common(row)
    values = CASE_READERS[case_id](row)
    assert len(values) == CASE_OBSERVATION_COUNTS[case_id]
    assert len({(value["requirement_id"], value["id"]) for value in values}) == len(values)
    _recheck()
    _write_observations(row, values)

