"""Independent P06h paired capture reader; stdlib/pytest only.

Required gates set IMAGEJXX_REQUIRE_RENDER_TESTS=1 and
IMAGEJXX_SHORTCUTS_UI_CASE_ROOT. Assertions read captured actual ImGui input and
state only; they never import imagejxx, create a renderer or launch a product.
Developer default skips without a root and establishes no acceptance. ImGui
delivery is distinct from OS keyboard/layout/accessibility qualification.
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
CASES = tuple(f"K{i:02}" for i in range(1, 11))
PARAMETERS = tuple((case, context) for case in CASES for context in CONTEXTS)
PARAMETER_IDS = tuple(case + "-" + context for case, context in PARAMETERS)
EXPECTED_COLLECTION_IDS = tuple("test_installed_shortcuts[" + value + "]" for value in PARAMETER_IDS)
EXPECTED_COLLECTION_COUNT = 20
MAX_REPORT = 16 * 1024 * 1024
MAX_ARCHIVE = 32 * 1024 * 1024
MAX_FRAMES = 180
MAX_SECONDS = 15
COMMANDS = (
    "file.open", "file.save", "file.close", "edit.undo", "edit.redo", "edit.copy",
    "edit.cut", "edit.paste", "edit.select_all", "analysis.measure", "app.preferences",
    "app.jobs", "view.channel_display", "image.auto_contrast", "image.extract_plane",
    "view.orthogonal", "filter.gaussian_imagej", "filter.rank_imagej",
)
DEFAULTS = {
    "file.open": "Ctrl+O", "file.save": "Ctrl+S", "file.close": "Ctrl+W",
    "edit.undo": "Ctrl+Z", "edit.redo": "Ctrl+Y", "edit.copy": "Ctrl+C",
    "edit.cut": "Ctrl+X", "edit.paste": "Ctrl+V", "edit.select_all": "Ctrl+A",
    "analysis.measure": "M",
}
FIXTURE_SHAPES = {"eager": [1, 1, 2, 6, 8, 1], "scientific": [2, 3, 2, 6, 8, 1]}
FRAME_KEYS = (
    "session", "input_frame", "shortcuts", "host_controls", "preferences_panel",
    "file_browser", "close_prompt", "dialog", "operation_dialog", "roi_manager",
    "jobs_panel", "channel_display_panel", "gaussian_panel", "rank_panel", "error_modal",
    "preferences_path", "window_error", "ordinary_canvas", "orthogonal_planes",
)
SOURCE_ROLES = {
    "observation_transport": "tests/python/actual_observation_transport.py",
    "observation_transport_header": "src/desktop/actual_observation_transport.hpp",
    "producer": "src/desktop/shortcuts_ui_probe.cpp",
    "producer_header": "src/desktop/shortcuts_ui_probe.hpp",
    "window": "src/desktop/window.cpp",
    "headless_header": "src/desktop/headless_session_internal.hpp",
    "input": "src/desktop/shortcut_input.cpp",
    "input_header": "src/desktop/shortcut_input.hpp",
    "editor": "src/desktop/shortcut_editor.cpp",
    "editor_header": "src/desktop/shortcut_editor.hpp",
    "settings_panel": "src/desktop/settings_panel.cpp",
    "settings_panel_header": "src/desktop/settings_panel.hpp",
    "native_core": "src/native/shortcuts.cpp",
    "native_header": "include/imagejxx/shortcuts.hpp",
    "native_settings": "src/native/settings.cpp",
    "native_preferences": "src/native/preferences.cpp",
    "desktop_binding": "src/desktop/python.cpp",
    "python_wrapper": "src/imagejxx/ui/app.py",
    "reader": "tests/python/test_shortcuts_ui.py",
    "adoption": "docs/parity/reviews/G012-shortcuts-adoption-001.json",
    "commands": "docs/parity/commands.json",
}
_PINS = {}


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


def _path(value):
    assert type(value) is str and value
    path = Path(value)
    assert path.is_absolute(), "Capture/provenance paths must be absolute"
    return path.resolve(strict=True)


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
    configured = os.environ.get("IMAGEJXX_SHORTCUTS_UI_CASE_ROOT")
    assert configured, "Required paired20 capture root is missing"
    root = _path(configured)
    assert root.is_dir()
    provenance = _json(root / "provenance.json", 2 * 1024 * 1024)
    assert provenance["schema_version"] == 1
    assert provenance["kind"] == "p06h.shortcuts-ui-reader-inputs.001"
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
        assert build["source_set_sha256"] == source_set == binding["source_set_sha256"]
        assert build["sources"] == sources
        for role in ("executable", "producer_artifact", "native_artifact"):
            assert build["products"][context][role] == binding[role]
        assert type(binding["elapsed_seconds"]) in (int, float) and 0 < binding["elapsed_seconds"] <= 60
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


PHASES = {
    "K01": ("before", "after_menu_hints", "after_open", "after_close", "after_dirty_close_cancel"),
    "K02": ("before", "after_select_all", "after_copy", "after_cut", "after_paste", "after_undo", "after_redo", "after_measure", "after_held_measure"),
    "K03": ("before", "after_assign_draft", "after_assigned_apply", "after_old_m_denial", "after_assigned_measure", "after_conflict", "after_reassign", "after_clear", "after_reset", "after_apply"),
    "K04": ("before", "after_text_denial", "after_numeric_denial", "after_menu_denial", "after_modal_denial", "after_file_denial", "after_panel_denial", "after_capture_escape"),
    "K05": ("before", "after_ordinary_local", "after_linked_local", "after_roi_primary_a", "after_gesture_denial", "after_gesture_cancel_release"),
    "K06": ("before", "while_busy", "after_busy_defaults", "after_busy_preferences", "after_busy_jobs", "after_cancel_requested", "after_busy_drained"),
    "K07": ("before", "after_assign_draft", "after_defaults", "after_revert", "after_cancel_draft", "after_apply", "after_export", "after_import", "before_failed_apply", "after_failed_apply", "after_failed_import"),
    "K08": ("before", "after_persist_false", "after_reopen_persist_false", "after_persist_true", "after_reopen_persist_true", "after_explicit_path", "after_env_path", "after_legacy_fallback", "after_env_blocks_legacy", "after_preferred_blocks_legacy"),
    "K09": ("before", "after_measure_denial", "after_channels", "after_auto_contrast", "after_plane_cancel", "after_xyz", "after_gaussian", "after_rank"),
    "K10": ("before", "after_extra_modifiers", "after_reservations", "after_unknown_row", "after_ambiguous_batch", "after_profile_handoff", "after_focus_handoff", "after_popup_handoff", "after_owner_handoff"),
}


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


def _source(row, phase):
    return _state(row, phase)["documents"]["source"]


def _draft(row, phase):
    return _report(row, phase)["preferences_panel"]["shortcut_editor"]["draft_override_lines"]


def _profile(row, phase, overrides):
    state = _state(row, phase)
    assert state["profile"]["overrides"] == overrides
    return _bindings(state)


def _unchanged_profile(a, b):
    for key in ("settings", "preferences", "profile"):
        assert a[key] == b[key]


def _capture(row, phase, key, candidate):
    _clicked(row, phase, "capture")
    _key(row, phase, key, ctrl=True)
    frames = _frames(row, phase)
    observed = [(frame["report"]["preferences_panel"]["shortcut_editor"],
                 frame["report"]["input_frame"]) for frame in frames]
    waiting = [index for index, (editor, _) in enumerate(observed)
               if editor["capture_state"] == "waiting_release"]
    armed = [index for index, (editor, physical) in enumerate(observed)
             if editor["capture_state"] == "armed" and not physical["keys_down"] and
             not any(physical["mouse_down"]) and not any(physical["modifiers"].values())]
    candidates = [index for index, (editor, physical) in enumerate(observed)
                  if editor["candidate"] == candidate and editor["capture_state"] == "idle" and
                  editor["release_gate"] is True and key in physical["keys_down"]]
    assert waiting and armed and candidates and min(waiting) < min(armed) < min(candidates)
    _no_dispatch(row, phase)


def _k01(row):
    initial = _state(row, "before")
    assert _bindings(initial) == {command: DEFAULTS.get(command) for command in COMMANDS}
    _literal(initial["documents"]["source"], "eager")
    hints = {}
    for frame in _frames(row, "after_menu_hints"):
        for value in frame["report"]["host_controls"].values():
            if "command_id" in value:
                hints[value["command_id"]] = value.get("shortcut_hint")
    assert set(hints) == set(COMMANDS)
    assert {command: hints[command] or None for command in COMMANDS} == {command: DEFAULTS.get(command) for command in COMMANDS}
    _key(row, "after_open", "O", ctrl=True)
    _dispatch(row, "after_open", "file.open")
    _clicked(row, "after_open", "file.open")
    opened = _state(row, "after_open")
    assert len(opened["document_ids"]) == len(initial["document_ids"]) + 1
    assert opened["documents"]["opened"]["data_identity"] == 0
    assert opened["documents"]["opened"]["id"] not in initial["document_ids"]
    _literal(opened["documents"]["opened"], "eager")
    for key in ("metadata", "calibration", "rois", "overlays"):
        assert opened["documents"]["opened"][key] == initial["documents"]["source"][key]
    _key(row, "after_close", "W", ctrl=True)
    _dispatch(row, "after_close", "file.close")
    assert _state(row, "after_close")["document_ids"] == initial["document_ids"]
    _key(row, "after_dirty_close_cancel", "W", ctrl=True)
    _dispatch(row, "after_dirty_close_cancel", "file.close")
    _clicked(row, "after_dirty_close_cancel", "dirty_close.cancel")
    final = _state(row, "after_dirty_close_cancel")
    assert final["document_ids"] == initial["document_ids"] and final["documents"]["source"]["dirty"] is True
    _literal(final["documents"]["source"], "eager")


def _k02(row):
    phases = PHASES["K02"]
    before = _source(row, "before")
    _literal(before, "eager")
    assert before["selection"] in (None, {})
    assert (before["history"]["undo_steps"], before["history"]["redo_steps"]) == (1, 0)
    operations = {"after_select_all": ("A", "edit.select_all"), "after_copy": ("C", "edit.copy"),
                  "after_cut": ("X", "edit.cut"), "after_paste": ("V", "edit.paste"),
                  "after_undo": ("Z", "edit.undo"), "after_redo": ("Y", "edit.redo")}
    for phase, (key, command) in operations.items():
        _key(row, phase, key, ctrl=True)
        _dispatch(row, phase, command)
    selected = _source(row, "after_select_all")["selection"]
    assert selected["kind"] == "rectangle" and selected["points"] == [[0, 0], [8, 6]]
    for phase in ("after_select_all", "after_copy", "after_paste", "after_redo", "after_measure", "after_held_measure"):
        _literal(_source(row, phase), "eager")
    for phase in ("after_cut", "after_undo"):
        document = _source(row, phase)
        assert document["shape"] == FIXTURE_SHAPES["eager"] and document["dtype"] == "uint8"
        assert [_plane(p, "uint8") for p in document["planes"]] == [
            [0] * 48, [7 + 3*y + x for y in range(6) for x in range(8)],
        ]
    for phase, undo, redo in (("after_select_all", 1, 0), ("after_copy", 1, 0),
                              ("after_cut", 2, 0), ("after_paste", 3, 0),
                              ("after_undo", 2, 1), ("after_redo", 3, 0)):
        history = _source(row, phase)["history"]
        assert (history["undo_steps"], history["redo_steps"]) == (undo, redo)
    assert _source(row, "after_undo")["data_identity"] == _source(row, "after_cut")["data_identity"]
    assert _source(row, "after_redo")["data_identity"] == _source(row, "after_paste")["data_identity"]
    _key(row, "after_measure", "M", released=False)
    _dispatch(row, "after_measure", "analysis.measure")
    measured = _state(row, "after_measure")["results"]
    assert len(measured) == len(_state(row, "after_redo")["results"]) + 1
    assert measured[-1]["count"] == 48 and measured[-1]["sum"] == 528
    assert abs(measured[-1]["mean"] - 11) <= 1e-12
    held = _frames(row, "after_held_measure")
    assert len(held) >= 24
    assert sum(frame["delta_time"] for frame in held) > max(frame["key_repeat_delay"] for frame in held)
    assert all("M" in frame["report"]["input_frame"]["keys_down"] for frame in held)
    assert not any(event["name"] == "M" and event["down"] is True
                   for frame in held for event in _atoms(frame["physical_inputs"]))
    checkpoint_index = _checkpoint(row, "after_held_measure")["frame_index"]
    release = row["frames"][checkpoint_index + 1:]
    assert release and any(event["name"] == "M" and event["down"] is False
                           for frame in release for event in _atoms(frame["physical_inputs"]))
    assert all(frame["report"]["shortcuts"]["dispatch"]["command_id"] is None for frame in release)
    _no_dispatch(row, "after_held_measure")
    assert _state(row, "after_held_measure")["results"] == measured
    for phase in phases[1:]:
        assert _source(row, phase)["metadata"] == before["metadata"]
        assert _source(row, phase)["rois"] == before["rois"]
    initial_workflow = _state(row, "before")["workflow"]
    for phase in ("after_select_all", "after_copy"):
        assert _state(row, phase)["workflow"] == initial_workflow
    cut_workflow = _state(row, "after_cut")["workflow"]
    assert len(cut_workflow) == len(initial_workflow) + 1 and cut_workflow[:-1] == initial_workflow
    assert cut_workflow[-1]["operation"] == "fill_color" and cut_workflow[-1]["slice_index"] == 0
    assert cut_workflow[-1]["all_slices"] is False and cut_workflow[-1]["selection"] == selected
    for phase in ("after_paste", "after_undo", "after_redo", "after_measure", "after_held_measure"):
        assert _state(row, phase)["workflow"] == cut_workflow


def _k03(row):
    _profile(row, "before", [])
    assert _draft(row, "after_assign_draft") == ["analysis.measure=Primary+K"]
    _capture(row, "after_assign_draft", "K", "Primary+K")
    _clicked(row, "after_assign_draft", "assign")
    assert _profile(row, "after_assign_draft", []) == {command: DEFAULTS.get(command) for command in COMMANDS}
    _clicked(row, "after_assigned_apply", "apply")
    assert _profile(row, "after_assigned_apply", ["analysis.measure=Primary+K"])["analysis.measure"] == "Ctrl+K"
    _key(row, "after_old_m_denial", "M")
    _no_dispatch(row, "after_old_m_denial")
    _same_data(_state(row, "after_assigned_apply"), _state(row, "after_old_m_denial"))
    _key(row, "after_assigned_measure", "K", ctrl=True)
    _dispatch(row, "after_assigned_measure", "analysis.measure")
    assert len(_state(row, "after_assigned_measure")["results"]) == len(_state(row, "after_old_m_denial")["results"]) + 1
    _capture(row, "after_conflict", "S", "Primary+S")
    assert _draft(row, "after_conflict") == ["analysis.measure=Primary+K"]
    diagnostics = _report(row, "after_conflict")["preferences_panel"]["shortcut_editor"]["candidate_diagnostics"]
    conflicts = [value for value in diagnostics if value["kind"] == "conflict"]
    assert len(conflicts) == 1
    assert {conflicts[0]["command_id"], conflicts[0]["other_command_id"]} == {"file.save", "analysis.measure"}
    assert set(conflicts[0]["platforms"]) == {"Windows", "Linux", "macOS"}
    _clicked(row, "after_reassign", "reassign_conflict")
    assert _draft(row, "after_reassign") == ["file.save=", "analysis.measure=Primary+S"]
    _clicked(row, "after_clear", "clear")
    assert _draft(row, "after_clear") == ["file.save=", "analysis.measure="]
    _clicked(row, "after_reset", "reset_command")
    assert _draft(row, "after_reset") == ["file.save="]
    for phase in ("after_conflict", "after_reassign", "after_clear", "after_reset"):
        _profile(row, phase, ["analysis.measure=Primary+K"])
        _same_data(_state(row, "after_assigned_measure"), _state(row, phase))
    _clicked(row, "after_apply", "apply")
    bindings = _profile(row, "after_apply", ["file.save="])
    assert bindings["file.save"] is None and bindings["analysis.measure"] == "M"
    assert _decode(_persisted(row, "after_apply"))["values"]["shortcut_bindings"] == ["file.save="]


def _k04(row):
    before = _state(row, "before")
    for phase in PHASES["K04"][1:]:
        _no_dispatch(row, phase)
        _same_data(before, _state(row, phase))
        _unchanged_profile(before, _state(row, phase))
    _clicked(row, "after_text_denial", "search")
    text = _report(row, "after_text_denial")["input_frame"]
    assert text["input_text"]["id"] != 0 and text["input_text"]["length"] > 0
    assert any(frame["report"]["shortcuts"]["dispatch"]["focus"] in {"text", "control"}
               for frame in _frames(row, "after_text_denial"))
    fields = _report(row, "after_numeric_denial")["preferences_panel"]["fields"]
    _clicked(row, "after_numeric_denial", "processing_memory_mb")
    field = fields["processing_memory_mb"]
    # InputInt keeps its last parsed typed value while its active text buffer
    # contains an incomplete '-'. Partial text does not become numeric zero.
    assert field["draft"] == before["settings"]["processing_memory_mb"] == 512
    assert field["active"] is True and field["focused"] is True
    numeric = _report(row, "after_numeric_denial")["input_frame"]["input_text"]
    assert numeric["id"] == field["id"] and numeric["length"] == 1
    _key(row, "after_numeric_denial", "M")
    numeric_presses = [frame for frame in _frames(row, "after_numeric_denial")
                       if any(event["name"] == "M" and event["down"]
                              for event in _atoms(frame["physical_inputs"]))]
    assert len(numeric_presses) == 1
    press = numeric_presses[0]["report"]
    press_field = press["preferences_panel"]["fields"]["processing_memory_mb"]
    assert press_field["id"] == field["id"] and press_field["active"] is True
    assert press["input_frame"]["input_text"]["id"] == field["id"]
    assert press["input_frame"]["input_text"]["length"] == 1
    assert press["shortcuts"]["dispatch"]["focus"] == "text"
    assert any(event["type"] == "text" and event["value"] == "-"
               for frame in _frames(row, "after_numeric_denial") for event in frame["physical_inputs"])
    for phase in ("after_menu_denial", "after_modal_denial", "after_file_denial"):
        assert any(frame["report"]["input_frame"]["popup_stack"] for frame in _frames(row, phase))
        assert any(frame["report"]["shortcuts"]["dispatch"]["focus"] == "modal"
                   for frame in _frames(row, phase))
    assert any(frame["report"]["shortcuts"]["dispatch"]["focus"] == "control"
               for frame in _frames(row, "after_panel_denial"))
    _key(row, "after_capture_escape", "Escape")
    editor = _report(row, "after_capture_escape")["preferences_panel"]["shortcut_editor"]
    assert editor["capture_state"] == "idle" and editor["candidate"] == ""
    assert editor["exit_reason"] == "escape"
    assert _report(row, "after_capture_escape")["preferences_panel"]["open"] is True
    for phase in ("after_text_denial", "after_numeric_denial", "after_menu_denial",
                  "after_modal_denial", "after_file_denial", "after_panel_denial"):
        assert any(list(_atoms(frame["physical_inputs"])) for frame in _frames(row, phase))


def _k05(row):
    before = _state(row, "before")
    _literal(before["documents"]["source"], "eager")
    for phase in PHASES["K05"][1:]:
        _no_dispatch(row, phase)
        _literal(_source(row, phase), "eager")
        assert _state(row, phase)["results"] == before["results"]
        assert _source(row, phase)["history"] == before["documents"]["source"]["history"]
        assert _source(row, phase)["dirty"] == before["documents"]["source"]["dirty"]
    for phase in ("after_ordinary_local", "after_linked_local"):
        _key(row, phase, "Right", shift=True)
        assert any("Right" in frame["report"]["shortcuts"]["dispatch"]["local_claimed_keys"]
                   for frame in _frames(row, phase))
    assert _source(row, "after_ordinary_local")["selection"] != before["documents"]["source"]["selection"]
    assert _source(row, "after_linked_local")["selection"] == _source(row, "after_ordinary_local")["selection"]
    # The genuine focus click also picks an XY voxel. Derive that pick from
    # actual delivered mouse coordinates and the observed transform, then apply
    # the independent +1 local Right rule. The final native crosshair supplies
    # neither the expected pick nor the expected nudge.
    pointers = [value for value in row["pointer_observations"]
                if value["phase"] == "after_linked_local" and
                value["selector"] == "actual_linked_canvas.XY"]
    assert len(pointers) == 1
    pointer = pointers[0]
    click_frames = [_integer(index, pointer["observed_frame"] + 1,
                             _checkpoint(row, "after_linked_local")["frame_index"])
                    for index in pointer["input_frames"]]
    down_frames = [index for index in click_frames
                   if any(event["type"] == "button" and event["name"] == "left" and event["down"]
                          for event in row["frames"][index]["physical_inputs"])]
    up_frames = [index for index in click_frames
                 if any(event["type"] == "button" and event["name"] == "left" and not event["down"]
                        for event in row["frames"][index]["physical_inputs"])]
    assert len(down_frames) == len(up_frames) == 1 and down_frames[0] < up_frames[0]
    click = row["frames"][down_frames[0]]["report"]
    xy = [plane for plane in click["orthogonal_planes"] if plane["plane"] == "XY"]
    assert len(xy) == 1 and xy[0]["visible"] is True and xy[0]["coordinate"] == 0
    assert xy[0]["source_document_id"] == before["documents"]["source"]["id"]
    mouse = click["input_frame"]["mouse_position"]
    assert len(mouse) == len(xy[0]["origin"]) == len(xy[0]["scale"]) == 2
    assert all(type(value) in (int, float) and math.isfinite(value)
               for value in mouse + xy[0]["origin"] + xy[0]["scale"])
    assert all(value > 0 for value in xy[0]["scale"])
    picked = [math.floor((mouse[axis] - xy[0]["origin"][axis]) / xy[0]["scale"][axis])
              for axis in range(2)]
    assert 0 <= picked[0] < 7 and 0 <= picked[1] < 6
    key_frames = [(index, frame) for index, frame in enumerate(row["frames"])
                  if frame["phase"] == "after_linked_local" and
                  any(event["name"] == "Right" and event["down"]
                      for event in _atoms(frame["physical_inputs"]))]
    assert len(key_frames) == 1 and up_frames[0] < key_frames[0][0]
    keyboard = [plane for plane in key_frames[0][1]["report"]["orthogonal_planes"]
                if plane["plane"] == "XY"]
    assert len(keyboard) == 1 and keyboard[0]["focused"] is True
    assert keyboard[0]["keyboard_admitted"] is True
    assert keyboard[0]["key_owners"]["Right"] == keyboard[0]["canvas_id"] == xy[0]["canvas_id"]
    assert _source(row, "after_linked_local")["display"]["crosshair"] == [picked[0] + 1, picked[1], 0]
    _key(row, "after_roi_primary_a", "A", ctrl=True)
    roi = _report(row, "after_roi_primary_a")["roi_manager"]
    assert roi["focused"] is True and len(roi["rows"]) >= 2
    assert roi["selected_indices"] == list(range(len(roi["rows"])))
    assert any(frame["report"]["shortcuts"]["dispatch"]["focus"] == "roi_manager"
               for frame in _frames(row, "after_roi_primary_a"))
    assert any(frame["report"]["shortcuts"]["dispatch"]["gesture_active"] is True
               for frame in _frames(row, "after_gesture_denial"))
    assert any(event["type"] == "button" and event["down"] is True
               for frame in _frames(row, "after_gesture_denial") for event in frame["physical_inputs"])
    assert any(event["type"] == "button" and event["down"] is False
               for frame in _frames(row, "after_gesture_cancel_release") for event in frame["physical_inputs"])
    assert _source(row, "after_gesture_cancel_release")["selection"] == _source(row, "after_roi_primary_a")["selection"]


def _k06(row):
    before = _state(row, "before")
    busy = _state(row, "while_busy")
    assert before["busy"] is False and busy["busy"] is True
    _same_data(before, busy)
    live_jobs = [job for job in busy["jobs"] if job["state_code"] in {0, 1}]
    assert live_jobs and all(type(job["id"]) is int and job["id"] > 0 for job in live_jobs)
    assert any(value["operation"] == "startBusyPreparation" for value in row["api_fixture_setup"])
    _no_dispatch(row, "after_busy_defaults")
    for key in ("O", "S", "W", "Z", "Y", "C", "X", "V", "A"):
        _key(row, "after_busy_defaults", key, ctrl=True)
    _key(row, "after_busy_defaults", "M")
    _same_data(busy, _state(row, "after_busy_defaults"))
    for command in DEFAULTS:
        assert any(frame["report"]["shortcuts"]["dispatch"]["availability"][command] is False
                   for frame in _frames(row, "after_busy_defaults"))
    _key(row, "after_busy_preferences", "F1")
    _dispatch(row, "after_busy_preferences", "app.preferences")
    assert _report(row, "after_busy_preferences")["preferences_panel"]["open"] is True
    _key(row, "after_busy_jobs", "F2")
    _dispatch(row, "after_busy_jobs", "app.jobs")
    assert _report(row, "after_busy_jobs")["jobs_panel"]["visible"] is True
    ids = {job["id"] for job in live_jobs}
    _clicked(row, "after_cancel_requested", "jobs.cancel")
    cancels = [value for value in row["control_observations"]
               if value["phase"] == "after_cancel_requested" and value["selector"] == "jobs.cancel"]
    assert len(cancels) == 1
    cancel = cancels[0]
    path = cancel["control_path"]
    assert len(path) == 4 and path[:2] == ["jobs_panel", "jobs"] and path[3] == "cancel"
    target = row["frames"][cancel["observed_frame"]]["report"]["jobs_panel"]["jobs"][_integer(path[2])]
    assert target["id"] in ids
    requested = _state(row, "after_cancel_requested")
    assert any(job["id"] in ids and job["cancel_requested"] is True for job in requested["jobs"])
    drained = _state(row, "after_busy_drained")
    assert drained["busy"] is False
    assert any(job["id"] in ids and job["state_code"] == 6 and job["cancel_requested"] is True for job in drained["jobs"])
    assert not any(job["state_code"] in {0, 1, 2, 3} for job in drained["jobs"])
    assert row["job_final"]["id"] in ids and row["job_final"]["state_code"] == 6
    assert row["job_final"]["cancel_requested"] is True and row["job_final"]["error"] == ""
    _same_data(busy, drained)


def _k07(row):
    before = _state(row, "before")
    for phase, suffix in (("after_assign_draft", "assign"), ("after_defaults", "defaults"),
                          ("after_revert", "revert"), ("after_cancel_draft", "close"),
                          ("after_apply", "apply"), ("after_export", "export"), ("after_import", "import")):
        _clicked(row, phase, suffix)
    assert _draft(row, "after_assign_draft") == ["analysis.measure=Primary+K"]
    assert _draft(row, "after_defaults") == []
    assert _draft(row, "after_revert") == before["profile"]["overrides"]
    for phase in ("after_assign_draft", "after_defaults", "after_revert", "after_cancel_draft"):
        _unchanged_profile(before, _state(row, phase))
        _same_data(before, _state(row, phase))
    assert _report(row, "after_cancel_draft")["preferences_panel"]["open"] is False
    _profile(row, "after_apply", ["analysis.measure=Primary+K"])
    _same_data(before, _state(row, "after_apply"))
    applied = _decode(_persisted(row, "after_apply"))
    exported = _decode(_persisted(row, "after_export", "exported_preferences"))
    assert exported == applied
    assert exported["values"]["shortcut_bindings"] == ["analysis.measure=Primary+K"]
    _same_data(_state(row, "after_apply"), _state(row, "after_import"))
    imported = _state(row, "after_import")
    assert _bindings(imported)["app.jobs"] == "F2"
    assert imported["profile"]["overrides"] == ["app.jobs=F2", "future.tool=opaque future v2"]
    assert imported["profile"]["inactive_rows"] == ["future.tool=opaque future v2"]
    assert _report(row, "after_import")["preferences_panel"]["shortcut_editor"]["inactive_rows"] == ["future.tool=opaque future v2"]
    assert _decode(_persisted(row, "after_import")) == imported["preferences"]
    assert imported["preferences"]["extras"]["future"] == "retained"
    assert imported["preferences"]["future_root"] == 17
    _clicked(row, "after_failed_apply", "apply")
    failed_before = _state(row, "before_failed_apply")
    failed_after = _state(row, "after_failed_apply")
    _same_data(failed_before, failed_after)
    _unchanged_profile(failed_before, failed_after)
    assert _persisted(row, "before_failed_apply", "parent_blocker") == _persisted(row, "after_failed_apply", "parent_blocker")
    assert all(value["exists"] is False for value in _checkpoint(row, "after_failed_apply")["files"]
               if value["role"] == "failed_apply_preferences")
    assert _report(row, "after_failed_apply")["preferences_panel"]["error"]
    _same_data(imported, _state(row, "after_failed_import"))
    _unchanged_profile(imported, _state(row, "after_failed_import"))
    assert _persisted(row, "after_import") == _persisted(row, "after_failed_import")
    assert any(value["phase"] == "after_failed_import" and value["type"] in {"ShortcutProfileError", "invalid_argument"}
               for value in row["exceptions"])


def _k08(row):
    _profile(row, "before", [])
    _profile(row, "after_persist_false", ["analysis.measure=Primary+K"])
    _profile(row, "after_reopen_persist_false", [])
    _profile(row, "after_persist_true", ["analysis.measure=Primary+K"])
    _profile(row, "after_reopen_persist_true", ["analysis.measure=Primary+K"])
    before_files = _checkpoint(row, "before")["files"]
    false_files = _checkpoint(row, "after_persist_false")["files"]
    assert [(f["role"], f["exists"]) for f in before_files] == [(f["role"], f["exists"]) for f in false_files]
    assert all(f["exists"] is False for f in false_files if f["role"] == "preferences")
    assert _decode(_persisted(row, "after_persist_true"))["values"]["shortcut_bindings"] == ["analysis.measure=Primary+K"]
    reopen = row["reopen_observations"]
    phases = ("after_reopen_persist_false", "after_reopen_persist_true", "after_explicit_path", "after_env_path",
              "after_legacy_fallback", "after_env_blocks_legacy", "after_preferred_blocks_legacy")
    assert [value["phase"] for value in reopen] == list(phases)
    for value in reopen:
        assert value["instance_kind"] == "fresh_native_window" and value["input_kind"] == "api_fixture_setup"
        assert value["state"] == _state(row, value["phase"])
        assert value["preferences_path"] == _report(row, value["phase"])["preferences_path"]
        assert type(value["preferences_path"]) is str and value["preferences_path"]
    expected_sources = ("persist_false_reopen", "persist_true_reopen", "explicit", "env", "legacy", "env", "preferred")
    assert tuple(value["source"] for value in reopen) == expected_sources
    assert row["environment_before"] == row["environment_after"]
    for phase in ("after_explicit_path", "after_env_path", "after_legacy_fallback", "after_preferred_blocks_legacy"):
        selected = _decode(_persisted(row, phase, "selected_preferences"))
        assert selected["values"]["shortcut_bindings"] == _state(row, phase)["profile"]["overrides"]
    for phase, overrides in (("after_explicit_path", ["analysis.measure=Primary+K"]),
                             ("after_env_path", ["app.jobs=F2"]),
                             ("after_legacy_fallback", ["analysis.measure=F3"]),
                             ("after_env_blocks_legacy", []),
                             ("after_preferred_blocks_legacy", ["app.jobs=F4"])):
        _profile(row, phase, overrides)
    missing = [value for value in _checkpoint(row, "after_env_blocks_legacy")["files"]
               if value["role"] == "selected_preferences"]
    assert len(missing) == 1 and missing[0]["exists"] is False
    legacy = _state(row, "after_legacy_fallback")
    preferred = next(value for value in _checkpoint(row, "after_legacy_fallback")["files"] if value["role"] == "preferred")
    assert preferred["exists"] is False and legacy["preferences_path"] == preferred["path"]


def _k09(row):
    _literal(_source(row, "before"), "scientific")
    _key(row, "after_measure_denial", "F9")
    for key in ("C", "X", "V"):
        _key(row, "after_measure_denial", key, ctrl=True)
    _no_dispatch(row, "after_measure_denial")
    for command in ("analysis.measure", "edit.copy", "edit.cut", "edit.paste"):
        assert any(frame["report"]["shortcuts"]["dispatch"]["availability"][command] is False
                   for frame in _frames(row, "after_measure_denial"))
    _same_data(_state(row, "before"), _state(row, "after_measure_denial"))
    commands = {"after_channels": "view.channel_display", "after_auto_contrast": "image.auto_contrast",
                "after_plane_cancel": "image.extract_plane", "after_xyz": "view.orthogonal",
                "after_gaussian": "filter.gaussian_imagej", "after_rank": "filter.rank_imagej"}
    keys = {"after_channels": "F5", "after_auto_contrast": "F8", "after_plane_cancel": "F6",
            "after_xyz": "F7", "after_gaussian": "F3", "after_rank": "F4"}
    for phase, command in commands.items():
        _key(row, phase, keys[phase])
        _dispatch(row, phase, command)
        _literal(_source(row, phase), "scientific")
        assert _source(row, phase)["history"] == _source(row, "before")["history"]
        assert _source(row, phase)["revisions"]["pixels"] == _source(row, "before")["revisions"]["pixels"]
        assert any(list(_atoms(frame["physical_inputs"])) for frame in _frames(row, phase))
    assert _report(row, "after_channels")["channel_display_panel"]["open"] is True
    _clicked(row, "after_plane_cancel", "plane.cancel")
    assert any(frame["report"]["operation_dialog"]["id"] == "axis_plane_export" and
               frame["report"]["dialog"]["open"] is True
               for frame in _frames(row, "after_plane_cancel"))
    assert _report(row, "after_plane_cancel")["dialog"]["open"] is False
    assert _source(row, "after_xyz")["display"]["orthogonal"] is True
    assert _report(row, "after_gaussian")["gaussian_panel"]["open"] is True
    assert _report(row, "after_rank")["rank_panel"]["open"] is True


def _k10(row):
    before = _state(row, "before")
    _key(row, "after_extra_modifiers", "S", ctrl=True, shift=True)
    _key(row, "after_extra_modifiers", "Z", ctrl=True, shift=True)
    _key(row, "after_extra_modifiers", "M", shift=True)
    _no_dispatch(row, "after_extra_modifiers")
    _same_data(before, _state(row, "after_extra_modifiers"))
    _unchanged_profile(before, _state(row, "after_reservations"))
    reserved = [value for value in row["exceptions"] if value["phase"] == "after_reservations"]
    assert reserved and any(d["kind"] == "reserved" for value in reserved for d in value["diagnostics"])
    unknown = _state(row, "after_unknown_row")
    assert unknown["profile"]["inactive_rows"] == ["future.tool=opaque unknown"]
    assert set(_bindings(unknown)) == set(COMMANDS)
    assert not any(command.startswith("future.") for command in _bindings(unknown))
    _literal(unknown["documents"]["source"], "eager")
    _no_dispatch(row, "after_unknown_row")
    ambiguous = _dispatches(row, "after_ambiguous_batch")
    assert any(value["reason"] == "ambiguous" and len(value["events"]) >= 2 for value in ambiguous)
    _no_dispatch(row, "after_ambiguous_batch")
    for phase in ("after_profile_handoff", "after_focus_handoff", "after_popup_handoff", "after_owner_handoff"):
        _no_dispatch(row, phase)
        frames = _frames(row, phase)
        assert any(frame["report"]["input_frame"]["keys_down"] for frame in frames)
        assert any(frame["report"]["shortcuts"]["dispatch"]["rearmed"] is False for frame in frames)
    assert any(frame["report"]["input_frame"]["popup_stack"] for frame in _frames(row, "after_popup_handoff"))
    owner_ids = [frame["report"]["shortcuts"]["dispatch"]["owner_id"] for frame in _frames(row, "after_owner_handoff")]
    assert len(set(owner_ids)) >= 2
    for phase in PHASES["K10"]:
        assert _state(row, phase)["workflow"] == before["workflow"]
        assert _state(row, phase)["results"] == before["results"]


CASE_READERS = dict(zip(CASES, (_k01, _k02, _k03, _k04, _k05, _k06, _k07, _k08, _k09, _k10)))


@pytest.mark.parametrize("case_id,context", PARAMETERS, ids=PARAMETER_IDS)
def test_installed_shortcuts(case_id, context):
    required = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert required in {"0", "1"}
    if required == "0" and not os.environ.get("IMAGEJXX_SHORTCUTS_UI_CASE_ROOT"):
        pytest.skip("Developer default has no paired capture root; no acceptance is established")
    row = _load(case_id, context)
    _common(row)
    CASE_READERS[case_id](row)
    _recheck()
