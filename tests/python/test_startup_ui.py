"""Independent SU01..SU10 paired actual ImGui startup reader.

No product imports or launch calls. These assertions qualify captured native
ImGui input/state only. API fixture construction and fresh Window observations
remain disclosed setup; C01..C12 in test_startup_launchers.py own OS relaunch.
Unexecuted clauses listed by the producer's scope_limits receive no credit.
"""
from functools import lru_cache
import hashlib
import os
from pathlib import Path

import pytest

from actual_observation_transport import decode_actual_observations
from startup_observation_reader import (
    CONTRACT, MIB, UI_CASES, UI_CONTEXTS, absolute, checkpoint, clicked,
    decode, documents, empty, file_bytes, fresh, integer, json_file,
    literal_document, pin, producer_envelope, provenance, recheck, report,
    state, typed_text, workspace,
)

CONTEXTS = UI_CONTEXTS
CASES = UI_CASES
PARAMETERS = tuple((case, context) for case in CASES for context in CONTEXTS)
PARAMETER_IDS = tuple(case + "-" + context for case, context in PARAMETERS)
EXPECTED_COLLECTION_IDS = tuple("test_installed_startup_workspace[" + name + "]" for name in PARAMETER_IDS)
EXPECTED_COLLECTION_COUNT = 20
FRAME_KEYS = ("session", "input_frame", "preferences_panel", "file_browser", "startup", "startup_controls",
              "preferences_path", "window_error", "error_modal", "host_controls", "document_tabs",
              "settings", "session_extensions", "ordinary_canvas", "orthogonal_planes", "document_info")
PHASES = {
    "SU01": ("default_empty", "prefs_visible"),
    "SU02": ("relative_resolved", "prefs_visible"),
    "SU03": ("restored_first", "tab_second", "tab_first"),
    "SU04": ("explicit_batch", "tab_first"),
    "SU05": ("before_failure", "explicit_refused", "prefs_visible"),
    "SU06": ("missing", "retry_missing", "current_bytes", "retry_current"),
    "SU07": ("legacy_fallback", "future_fallback", "known_invalid", "start_empty"),
    "SU08": ("initial", "browse_canceled", "browse_draft", "draft_closed", "defaults", "revert", "applied", "reopened"),
    "SU09": ("recovered", "recovered_applied", "missing", "chosen", "disabled"),
    "SU10": ("loaded", "one_shot_ignored", "reset_before", "reset_persisted", "save_failed"),
}
SOURCE_ROLES = {
    "observation_transport": "tests/python/actual_observation_transport.py",
    "observation_transport_header": "src/desktop/actual_observation_transport.hpp",
    "producer": "src/desktop/startup_ui_probe.cpp",
    "producer_header": "src/desktop/startup_ui_probe.hpp",
    "startup_panel": "src/desktop/startup_panel.cpp", "startup_panel_header": "src/desktop/startup_panel.hpp",
    "window": "src/desktop/window.cpp", "window_header": "src/desktop/window.hpp",
    "headless_header": "src/desktop/headless_session_internal.hpp",
    "settings_panel": "src/desktop/settings_panel.cpp",
    "settings_panel_header": "src/desktop/settings_panel.hpp",
    "native_settings": "src/native/settings.cpp",
    "native_preferences": "src/native/preferences.cpp",
    "startup_core": "src/native/startup.cpp", "startup_header": "include/imagejxx/startup.hpp",
    "desktop_binding": "src/desktop/python.cpp", "desktop_main": "src/desktop/main.cpp",
    "python_wrapper": "src/imagejxx/ui/app.py", "python_preferences": "src/imagejxx/ui/persistence.py",
    "reader": "tests/python/test_startup_ui.py", "reader_primitives": "tests/python/startup_observation_reader.py",
    "python_launcher": "scripts/capture_startup_workspace_ui.py",
    "capture_supervisor": "scripts/capture_shortcuts_history_ui.py",
    "process_helper": "scripts/capture_windows_platform_workload.py",
}


def _qualified_reader_origin(role, source, archived):
    import ast
    actual = {"bytes": source.stat().st_size,
              "sha256": hashlib.sha256(source.read_bytes()).hexdigest()}
    if actual == {key: archived[key] for key in ("bytes", "sha256")}:
        return
    # A reviewed reader correction consumes the unchanged captured products,
    # source closure and raw inputs. It never rewrites their provenance.
    configured = os.environ.get("IMAGEJXX_STARTUP_UI_READER_REPAIR")
    assert configured, "Changed reader requires an explicit pinned repair"
    repair_path = absolute(configured)
    repair_pin = {"path": str(repair_path),
                  "bytes": int(os.environ["IMAGEJXX_STARTUP_UI_READER_REPAIR_BYTES"]),
                  "sha256": os.environ["IMAGEJXX_STARTUP_UI_READER_REPAIR_SHA256"]}
    repair = json_file(pin(repair_pin), 64 * MIB)
    assert repair["schema_version"] == 1 and repair["state"] == "reviewed_source_only"
    assert repair["id"] == "p06j.startup.typed-text-temporal-binding.001"
    assert set(repair["sources"]) == {"reader", "reader_primitives"}
    entry = repair["sources"][role]
    assert {key: entry["before"][key] for key in actual} == {key: archived[key] for key in actual}
    assert {key: entry["after"][key] for key in actual} == actual
    assert pin(entry["after"]) == source.absolute()
    before_tree = ast.parse(pin(entry["before"]).read_text(encoding="utf-8"))
    after_tree = ast.parse(source.read_text(encoding="utf-8"))
    allowed = {"typed_text"} if role == "reader_primitives" else {"inputs", "_qualified_reader_origin"}
    def retained(tree):
        return ast.dump(ast.Module(body=[node for node in tree.body
            if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) or node.name not in allowed],
            type_ignores=[]), include_attributes=False)
    assert retained(before_tree) == retained(after_tree), "Unrelated reader source changed"
    if role == "reader_primitives":
        def assertions(tree):
            function = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "typed_text")
            return [ast.dump(node, include_attributes=False) for node in ast.walk(function) if isinstance(node, ast.Assert)]
        assert assertions(before_tree) == assertions(after_tree), "Text focus/value assertions changed"


@lru_cache(maxsize=1)
def inputs():
    configured = os.environ.get("IMAGEJXX_STARTUP_UI_CASE_ROOT")
    assert configured, "Required paired20 startup UI root is missing"
    root, value, roles = provenance(configured, kind="p06j.startup-workspace-ui-reader-inputs.001",
                                    contexts=CONTEXTS, cases=CASES, source_roles=SOURCE_ROLES)
    for role, source in (("reader", Path(__file__)),
                         ("reader_primitives", Path(__file__).with_name("startup_observation_reader.py")),
                         ("observation_transport", Path(__file__).with_name("actual_observation_transport.py"))):
        _qualified_reader_origin(role, source, roles[role])
    return root, value


def common(raw):
    assert [row["phase"] for row in raw["checkpoints"]] == list(PHASES[raw["case_id"]])
    assert 1 <= len(raw["frames"]) <= 180
    projection = raw["frame_projection"]
    assert projection["schema"] == "imagejxx.startup-workspace-ui-frame-projection" and projection["version"] == 1
    assert projection["keys"] == list(FRAME_KEYS)
    sessions = {}
    for frame in raw["frames"]:
        assert frame["phase"] in PHASES[raw["case_id"]] + ("fixture_setup",)
        captured = frame["report"]
        assert set(FRAME_KEYS[:13]) <= set(captured) <= set(FRAME_KEYS)
        session = captured["session"]
        assert session["open"] is True and session["context_id"] == session["current_context_id"] != 0
        identity = integer(session["session_id"], 1)
        number = integer(session["imgui_frame"], 1)
        assert number > sessions.get(identity, 0)
        sessions[identity] = number
        physical = captured["input_frame"]
        assert physical["raw_inputs"] == frame["physical_inputs"] and len(frame["physical_inputs"]) <= 120
        assert type(physical["focus"]["window"]) is str
        for event in frame["physical_inputs"]:
            assert event["type"] in {"position", "button", "key", "keys", "text", "wheel"}
    for phase in PHASES[raw["case_id"]]:
        actual = state(raw, phase)
        owners = documents(actual)
        assert actual["active_document_id"] in actual["document_ids"] or not actual["document_ids"]
        assert actual["busy"] is False
        assert actual["settings"] == actual["preferences"]["values"]
        assert actual["startup"] == report(raw, phase)["startup"]
        assert actual["startup"]["document_ids"] == actual["document_ids"]
        for owner in owners:
            integer(owner["id"], 1)
            assert type(owner["dirty"]) is bool
        for file in checkpoint(raw, phase)["files"]:
            file_bytes(raw, phase, file["role"])
    for key in ("api_fixture_setup", "exceptions", "reopen_observations"):
        for item in raw[key]:
            assert item["input_kind"] == "api_fixture_setup"
    for item in raw["reopen_observations"]:
        assert item["instance_kind"] == "fresh_native_window" and item["os_relaunch"] is False
        assert type(item["state"]) is dict and item["state"]["document_ids"] == item["state"]["startup"]["document_ids"]
    assert type(raw["scope_limits"]) is list and raw["scope_limits"]


def same_membership(before, after):
    for key in ("document_ids", "active_document_id", "documents", "results", "workflow"):
        assert before[key] == after[key], key


def unchanged_preferences(raw, before, after):
    assert file_bytes(raw, before) == file_bytes(raw, after)
    assert state(raw, before)["settings"] == state(raw, after)["settings"]


def diagnostics(value, reason):
    rows = value["startup"]["diagnostics"]
    assert any(row["reason"] == reason for row in rows), (reason, rows)
    for row in rows:
        assert len(row["path"].encode("utf-8")) <= 4096
        assert len(row["detail"].encode("utf-8")) <= 1024
        assert type(row["recoverable"]) is bool


def su01(raw):
    initial, visible = (state(raw, phase) for phase in PHASES["SU01"])
    empty(initial); empty(visible)
    assert initial["startup"]["source"] == "empty"
    assert initial["settings"]["startup_mode"] == "empty" and initial["settings"]["startup_workspace"] == ""
    assert initial["preferences"]["extras"]["recent"]
    clicked(raw, "prefs_visible", "revert")
    unchanged_preferences(raw, "default_empty", "prefs_visible")


def su02(raw):
    selected = state(raw, "relative_resolved")
    workspace(selected)
    startup = selected["startup"]
    assert startup["source"] == "persisted_workspace" and len(startup["paths"]) == 1
    configured = selected["settings"]["startup_workspace"]
    assert not Path(configured).is_absolute()
    expected = (Path(startup["preferences_write_path"]).parent / configured).resolve()
    assert Path(startup["paths"][0]) == expected
    clicked(raw, "prefs_visible", "revert")
    same_membership(selected, state(raw, "prefs_visible"))


def su03(raw):
    restored = state(raw, "restored_first")
    workspace(restored, active=0)
    second = state(raw, "tab_second"); first = state(raw, "tab_first")
    workspace(second, active=1); workspace(first, active=0)
    assert restored["document_ids"] == second["document_ids"] == first["document_ids"]
    clicked(raw, "tab_second", "document_tabs.1")
    clicked(raw, "tab_first", "document_tabs.0")


def su04(raw):
    batch = state(raw, "explicit_batch")
    rows = documents(batch)
    assert len(rows) == 3
    literal_document(rows[1], False); literal_document(rows[2], True)
    assert batch["startup"]["source"] == "explicit_paths" and len(batch["startup"]["paths"]) == 2
    assert batch["active_document_id"] == batch["document_ids"][1]
    assert batch["results"] == [{"Value": 17}] and batch["workflow"] == [{"operation": "must_never_execute", "historical_id": 777}]
    selected = state(raw, "tab_first")
    assert selected["document_ids"] == batch["document_ids"] and selected["active_document_id"] == selected["document_ids"][0]
    clicked(raw, "tab_first", "document_tabs.0")


def su05(raw):
    before, refused, visible = (state(raw, phase) for phase in PHASES["SU05"])
    empty(before); empty(refused); empty(visible)
    unchanged_preferences(raw, "before_failure", "explicit_refused")
    assert any(row["type"] == "StartupError" and row.get("diagnostic", {}).get("reason") == "read_failed"
               and row["diagnostic"]["path"].endswith("missing.pgm") for row in raw["exceptions"])
    clicked(raw, "prefs_visible", "revert")


def su06(raw):
    missing = state(raw, "missing"); failed = state(raw, "retry_missing")
    empty(missing); empty(failed)
    diagnostics(missing, "read_failed"); diagnostics(failed, "read_failed")
    assert missing["startup"]["retry_allowed"] is True
    clicked(raw, "retry_missing", "startup.retry")
    unchanged_preferences(raw, "missing", "retry_missing")
    empty(state(raw, "current_bytes"))
    assert file_bytes(raw, "current_bytes", "workspace") == file_bytes(raw, "retry_current", "workspace")
    restored = state(raw, "retry_current")
    workspace(restored, active=0, changed=True)
    clicked(raw, "retry_current", "startup.retry")
    unchanged_preferences(raw, "missing", "retry_current")


def su07(raw):
    legacy, future, invalid, cleared = (state(raw, phase) for phase in PHASES["SU07"])
    workspace(legacy, active=1); workspace(future, active=1)
    diagnostics(legacy, "missing_presentation"); diagnostics(future, "future_presentation")
    assert future["extensions"]["imagejxx_workspace"]["version"] == 9
    empty(invalid); diagnostics(invalid, "invalid_presentation")
    empty(cleared)
    clicked(raw, "start_empty", "startup.start_empty")
    unchanged_preferences(raw, "known_invalid", "start_empty")


def su08(raw):
    initial = state(raw, "initial")
    for phase in ("browse_canceled", "browse_draft", "draft_closed", "defaults", "revert"):
        same_membership(initial, state(raw, phase))
        unchanged_preferences(raw, "initial", phase)
    clicked(raw, "browse_canceled", "startup_workspace.browse")
    clicked(raw, "browse_canceled", "file_browser.controls.cancel")
    clicked(raw, "browse_draft", "startup_workspace.browse")
    clicked(raw, "browse_draft", "file_browser.controls.submit")
    chosen_draft = report(raw, "browse_draft")["preferences_panel"]["fields"]["startup_workspace"]["draft"]
    assert Path(chosen_draft).is_absolute() and Path(chosen_draft).name == "chosen.ijx"
    typed_text(raw, "browse_draft", "startup_workspace.editor", chosen_draft)
    clicked(raw, "browse_draft", "startup_mode.choices.empty")
    clicked(raw, "browse_draft", "startup_mode.choices.workspace")
    clicked(raw, "draft_closed", "close_button")
    clicked(raw, "defaults", "defaults")
    clicked(raw, "revert", "revert")
    for phase in ("browse_canceled", "draft_closed", "defaults", "revert"):
        fields = report(raw, phase)["preferences_panel"]["fields"]
        assert fields["startup_mode"]["draft"] == "empty" and fields["startup_workspace"]["draft"] == ""
    clicked(raw, "applied", "apply")
    applied = state(raw, "applied")
    assert applied["settings"]["startup_mode"] == "workspace"
    chosen = applied["settings"]["startup_workspace"]
    assert Path(chosen).is_absolute() and Path(chosen).suffix == ".ijx"
    assert applied["settings"]["shortcut_bindings"] == initial["settings"]["shortcut_bindings"]
    same_membership(initial, applied)
    assert decode(file_bytes(raw, "applied"))["values"]["startup_workspace"] == chosen
    reopened = state(raw, "reopened")
    workspace(reopened)
    assert reopened["settings"] == applied["settings"]
    assert any(item["phase"] == "reopened" for item in raw["reopen_observations"])


def su09(raw):
    recovered = state(raw, "recovered")
    empty(recovered)
    assert recovered["settings"]["startup_mode"] == "empty" and recovered["settings"]["startup_workspace"] == ""
    clicked(raw, "recovered_applied", "defaults")
    clicked(raw, "recovered_applied", "apply")
    clicked(raw, "recovered", "group_choices.Startup")
    assert file_bytes(raw, "recovered_applied", "recovery_backup") == file_bytes(raw, "recovered")
    assert decode(file_bytes(raw, "recovered"))["values"]["startup_mode"] == "invalid-mode"
    applied = state(raw, "recovered_applied")
    assert applied["settings"]["startup_mode"] == "empty" and applied["settings"]["startup_workspace"] == ""
    assert applied["settings"]["shortcut_bindings"] == recovered["settings"]["shortcut_bindings"]
    missing = state(raw, "missing")
    empty(missing); diagnostics(missing, "read_failed")
    clicked(raw, "chosen", "startup.choose_workspace")
    clicked(raw, "chosen", "file_browser.controls.submit")
    chosen = state(raw, "chosen")
    empty(chosen)
    assert chosen["settings"]["startup_mode"] == "workspace"
    assert Path(chosen["settings"]["startup_workspace"]).is_absolute()
    assert chosen["settings"]["startup_workspace"] != missing["settings"]["startup_workspace"]
    clicked(raw, "disabled", "startup.disable_restore")
    disabled = state(raw, "disabled")
    assert disabled["settings"]["startup_mode"] == "empty"
    same_membership(chosen, disabled)
    assert decode(file_bytes(raw, "disabled"))["values"]["startup_mode"] == "empty"


def su10(raw):
    loaded = state(raw, "loaded")["startup"]["layout"]
    ignored = state(raw, "one_shot_ignored")["startup"]["layout"]
    assert loaded["saved_applied"] is True and loaded["loaded_bytes"] > 0
    assert ignored["saved_applied"] is False and ignored["loaded_bytes"] == 0
    actual_loaded = checkpoint(raw, "loaded")["actual_layout"]
    actual_ignored = checkpoint(raw, "one_shot_ignored")["actual_layout"]
    loaded_window = [row for row in actual_loaded["windows"] if row["name"] == "Preferences"]
    ignored_window = [row for row in actual_ignored["windows"] if row["name"] == "Preferences"]
    assert len(loaded_window) == len(ignored_window) == 1
    assert loaded_window[0]["position"] == [60., 70.]
    assert ignored_window[0]["position"] != [60., 70.]
    assert "[Window][startup_layout_seed]" in actual_loaded["ini"]
    assert state(raw, "one_shot_ignored")["startup"]["reset_layout"] is True
    before = state(raw, "reset_before"); persisted = state(raw, "reset_persisted")
    clicked(raw, "reset_persisted", "reset_layout")
    assert persisted["startup"]["layout"]["reset_generation"] == before["startup"]["layout"]["reset_generation"] + 1
    assert "layout" not in decode(file_bytes(raw, "reset_persisted"))["extras"]
    same_membership(before, persisted)
    clicked(raw, "save_failed", "reset_layout")
    failure = state(raw, "save_failed")
    assert failure["startup"]["layout"]["reset_generation"] == 0
    assert report(raw, "save_failed")["preferences_panel"]["error"]
    same_membership(persisted, failure)


ASSERTIONS = {"SU01": su01, "SU02": su02, "SU03": su03, "SU04": su04, "SU05": su05,
              "SU06": su06, "SU07": su07, "SU08": su08, "SU09": su09, "SU10": su10}


@pytest.mark.parametrize("case,context", PARAMETERS, ids=PARAMETER_IDS)
def test_installed_startup_workspace(case, context):
    if not os.environ.get("IMAGEJXX_STARTUP_UI_CASE_ROOT"):
        if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
            pytest.fail("Required paired20 startup UI captures are missing")
        pytest.skip("No paired startup UI observation root; no acceptance established")
    root, value = inputs()
    descriptor = next(item for item in value["cases"] if (item["id"], item["context"]) == (case, context))
    raw = decode_actual_observations(json_file(pin(descriptor["raw"])), max_expanded_bytes=64 * MIB)
    producer_envelope(raw, descriptor, value["contexts"][context], schema="imagejxx.startup-workspace-ui-observations",
                      case=case, context=context)
    assert raw["runtime_before"]["producer_source"].replace("\\", "/").endswith("/src/desktop/startup_ui_probe.cpp")
    common(raw)
    ASSERTIONS[case](raw)
    recheck()
