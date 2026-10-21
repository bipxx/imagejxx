"""Independent C01..C12 ordinary C++ / python -m launcher observations.

This stdlib/pytest reader never imports imagejxx or starts a process. It checks
recorded OS launches, native normal-loop state and immutable bytes. Failed
constructor launches have no state artifact: their exit/log/file assertions do
not claim an unobserved Window membership or private ImGui input outcome.
"""
from functools import lru_cache
import hashlib
import os
from pathlib import Path
import struct
import zlib

import pytest

from startup_observation_reader import (
    LAUNCH_CASES, LAUNCH_CONTEXTS, MIB, absolute, decode, integer, json_file,
    pin, provenance, recheck,
)

CASES, CONTEXTS = LAUNCH_CASES, LAUNCH_CONTEXTS
PARAMETERS = tuple((case, context) for case in CASES for context in CONTEXTS)
PARAMETER_IDS = tuple(case + "-" + context for case, context in PARAMETERS)
EXPECTED_COLLECTION_IDS = tuple("test_installed_normal_startup_launcher[" + name + "]" for name in PARAMETER_IDS)
EXPECTED_COLLECTION_COUNT = 24
SOURCE_ROLES = {
    "launcher_capture": "scripts/capture_startup_launchers.py",
    "capture_supervisor": "scripts/capture_shortcuts_history_ui.py",
    "process_helper": "scripts/capture_windows_platform_workload.py",
    "reader": "tests/python/test_startup_launchers.py", "reader_primitives": "tests/python/startup_observation_reader.py",
    "window": "src/desktop/window.cpp", "window_header": "src/desktop/window.hpp",
    "desktop_main": "src/desktop/main.cpp", "desktop_binding": "src/desktop/python.cpp",
    "python_package": "src/imagejxx/__init__.py", "python_wrapper": "src/imagejxx/ui/app.py",
    "python_entry": "src/imagejxx/ui/__main__.py", "python_ui_init": "src/imagejxx/ui/__init__.py",
    "python_preferences": "src/imagejxx/ui/persistence.py",
    "startup_core": "src/native/startup.cpp", "startup_header": "include/imagejxx/startup.hpp",
    "native_preferences": "src/native/preferences.cpp", "native_settings": "src/native/settings.cpp",
    "session": "src/native/session.cpp", "application": "src/native/application.cpp",
    "runtime_observer": "src/desktop/startup_ui_probe.cpp", "runtime_observer_header": "src/desktop/startup_ui_probe.hpp",
}


@lru_cache(maxsize=1)
def inputs():
    configured = os.environ.get("IMAGEJXX_STARTUP_LAUNCHER_CASE_ROOT")
    assert configured, "Required normal launcher24 observation root is missing"
    root, value, roles = provenance(configured, kind="p06j.normal-launcher-reader-inputs.001",
                                    contexts=CONTEXTS, cases=CASES, source_roles=SOURCE_ROLES)
    for role, source in (("reader", Path(__file__)),
                         ("reader_primitives", Path(__file__).with_name("startup_observation_reader.py"))):
        assert source.stat().st_size == roles[role]["bytes"]
        assert hashlib.sha256(source.read_bytes()).hexdigest() == roles[role]["sha256"]
    assert set(value["installed_modules"]) == {"imagejxx", "imagejxx._native", "imagejxx._desktop",
                                               "imagejxx.ui.app", "imagejxx.ui.persistence"}
    for module in value["installed_modules"].values(): pin(module)
    raw_bytes = 0
    for descriptor in value["cases"]:
        raw = json_file(pin(descriptor["raw"]))
        raw_bytes += descriptor["raw"]["bytes"]
        if raw["observation"] is not None:
            pin(raw["observation"])
            raw_bytes += raw["observation"]["bytes"]
    assert raw_bytes <= 32*MIB
    return root, value


def snapshot(raw, phase, role):
    item = raw["files_" + phase][role]
    assert type(item["exists"]) is bool and Path(item["source_path"]).is_absolute()
    if not item["exists"]:
        assert item["snapshot"] is None and item["byte_count"] == 0 and item["hex"] == ""
        return None
    raw_bytes = bytes.fromhex(item["hex"])
    assert len(raw_bytes) == integer(item["byte_count"], 1, 4*MIB)
    retained = pin(item["snapshot"])
    assert retained.read_bytes() == raw_bytes
    return raw_bytes


def wire(raw):
    assert raw[:10] == b"IMAGEJXX3\n" and len(raw) <= 4*MIB
    declared = struct.unpack("<Q", raw[10:18])[0]
    assert 0 < declared <= 4*MIB
    inflater = zlib.decompressobj()
    payload = inflater.decompress(raw[18:], declared + 1)
    assert len(payload) == declared and inflater.eof and not inflater.unused_data and not inflater.unconsumed_tail
    return decode(payload)


def native_runtime(observed, binding, raw, provenance_value):
    runtime = observed["runtime_before"]
    assert runtime == observed["runtime_after"]
    assert runtime["process_id"] == raw["process"]["pid"]
    assert runtime["pointer_bits"] == 64 and runtime["imgui"]
    for role in ("executable", "producer_artifact", "native_artifact"):
        assert {key: runtime[role][key] for key in ("bytes", "sha256")} == {key: binding[role][key] for key in ("bytes", "sha256")}
        assert pin(runtime[role]) == pin(binding[role])
    if raw["context"] == "python":
        assert observed["python_process_id"] == runtime["process_id"]
        assert set(observed["module_origins"]) == set(provenance_value["installed_modules"])
        for name, actual in observed["module_origins"].items():
            expected = provenance_value["installed_modules"][name]
            assert actual["bytes"] == expected["bytes"] and actual["sha256"] == expected["sha256"]
            assert pin(actual) == pin(expected)


def samples(document, scientific, changed=False):
    shape = [2, 3, 2, 6, 8, 1] if scientific else [1, 1, 2, 6, 8, 1]
    assert document["shape"] == shape
    assert document["dtype"] == ("uint16" if scientific else "uint8")
    assert document["all_samples_observed"] is True
    expected = [[40*t + 30*c + 5*z + 2*y + x if scientific else 7*z + 3*y + x
                 for y in range(6) for x in range(8)]
                for t in range(shape[0]) for c in range(shape[1]) for z in range(2)]
    if changed and scientific: expected[11][19] = 113
    assert document["planes"] == expected
    assert document["dirty"] is False and document["undo_steps"] == document["redo_steps"] == 0
    assert document["revisions"] == {"pixels": 0, "metadata": 0, "selection": 0, "display": 0}
    assert document["calibration"]["spacing"] == [1.25, .5, 2.5]
    assert document["calibration"]["origin"] == [-2., 3., 4.]
    assert document["calibration"]["frame_interval"] == .75 and document["calibration"]["time_origin"] == 2.
    assert document["current_tcz"] == ([1, 2, 1] if scientific else [0, 0, 1])
    display = document["display"]
    assert display["crosshair"] == [3, 2, 1] and display["zoom"] == 2.25
    assert display["plane_zoom"] == [1.5, 2.5, 3.5]
    assert display["pan"] == [[.25, -.5], [1.25, -1.5], [2.25, -2.5]]
    assert display["orthogonal"] is scientific and display["linked_zoom"] is False


def loaded_workspace(observed, *, changed=False):
    docs = observed["document_infos"]
    assert len(docs) == 2
    samples(docs[0], False); samples(docs[1], True, changed)
    assert observed["startup"]["document_ids"] == [doc["id"] for doc in docs]
    assert observed["startup"]["active_index"] == 0
    assert observed["results"] == [{"Value": 17}]
    assert observed["workflow"] == [{"operation": "must_never_execute", "historical_id": 777}]
    assert observed["session_extensions"] == {"imagejxx_workspace": {"version": 1, "active_index": 0},
                                                "vendor": {"retained": True}}


def command(raw, binding, provenance_value):
    process = raw["process"]
    assert process["state"] == "exited" and process["owned_job_pids_after_cleanup"] == []
    assert type(process["elapsed_seconds"]) in (int, float) and 0 < process["elapsed_seconds"] <= 25
    assert type(process["timeout_seconds"]) in (int, float) and 0 < process["timeout_seconds"] <= 15
    integer(process["pid"], 1); integer(process["creation_filetime"], 1)
    argv = process["argv"]
    assert type(argv) is list and 1 <= len(argv) <= 64 and all(type(item) is str for item in argv)
    assert Path(argv[0]).resolve() == pin(binding["executable"])
    assert "--hidden" in argv and argv[argv.index("--frames") + 1] == "3"
    assert argv.count("--startup-observe") == argv.count("--preferences") == 1
    assert not any("ui-case" in item or "consumer" in item or "smoke" in item or "headless" in item for item in argv)
    assert Path(argv[argv.index("--preferences") + 1]) == Path(raw["files_before"]["preferences"]["source_path"])
    assert Path(process["cwd"]).is_absolute()
    assert process["environment"]["PYTHONNOUSERSITE"] == "1" and process["environment"]["PYTHONDONTWRITEBYTECODE"] == "1"
    assert set(process["removed_environment"]) == {"PYTHONHOME", "IMAGEJXX_CONFIG_DIR", "IMAGEJXX_PREFERENCES"}
    if raw["context"] == "python":
        assert argv[1:6] == ["-X", "utf8", "-B", "-m", "imagejxx.ui"]
        expected = os.pathsep.join((provenance_value["contexts"]["python"]["installed_root"], provenance_value["numpy"]["site_packages"]))
        assert process["environment"]["PYTHONPATH"] == expected
        cache = Path(process["environment"]["PYTHONPYCACHEPREFIX"])
        assert cache.is_absolute() and not cache.exists()
    else:
        assert "-m" not in argv and "PYTHONPATH" not in process["environment"]
    return pin(process["stdout"], allow_empty=True).read_bytes(), pin(process["stderr"], allow_empty=True).read_bytes()


@pytest.mark.parametrize("case,context", PARAMETERS, ids=PARAMETER_IDS)
def test_installed_normal_startup_launcher(case, context):
    if not os.environ.get("IMAGEJXX_STARTUP_LAUNCHER_CASE_ROOT"):
        if os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1":
            pytest.fail("Required ordinary startup launcher24 observations are missing")
        pytest.skip("No ordinary launcher startup captures; no relaunch acceptance established")
    root, value = inputs()
    descriptor = next(row for row in value["cases"] if (row["id"], row["context"]) == (case, context))
    raw = json_file(pin(descriptor["raw"]))
    assert raw["schema"] == "imagejxx.startup-normal-launcher-observations" and raw["version"] == 1
    assert (raw["case_id"], raw["context"]) == (case, context)
    assert raw["contract"] == "p06j.saved-workspace-startup.001"
    assert raw["observation_state"] == "observed" and raw["acceptance_claim"] is False
    assert raw["input_kind"] == "normal_os_launcher"
    assert not {"expected", "PASS", "pass", "passed", "outcome", "result"}.intersection(raw)
    assert raw["fixture_setup"] == {"input_kind": "stdlib_owned_wire_literal", "product_imported": False,
                                      "formula_u8": "7*z+3*y+x", "formula_u16": "40*t+30*c+5*z+2*y+x"}
    binding = value["contexts"][context]
    stdout, stderr = command(raw, binding, value)
    for phase in ("before", "after"):
        assert set(raw["files_" + phase]) == {"preferences", "workspace", "workspace_original", "first", "third", "missing", "configured_missing"}
        for role in raw["files_" + phase]: snapshot(raw, phase, role)
    for role in ("workspace", "workspace_original", "first", "third", "missing", "configured_missing"):
        assert snapshot(raw, "before", role) == snapshot(raw, "after", role)
    assert snapshot(raw, "before", "missing") is None and snapshot(raw, "before", "configured_missing") is None
    assert snapshot(raw, "before", "first") == b"P5\n8 6\n255\n" + bytes(3+y+x for y in range(6) for x in range(8))
    original = wire(snapshot(raw, "before", "workspace_original"))
    current = wire(snapshot(raw, "before", "workspace"))
    assert original["extensions"]["imagejxx_workspace"] == {"version": 1, "active_index": 0}
    if case == "C11": assert current != original
    else: assert current == original
    expected_exit = 2 if case in ("C06", "C07") and context == "python" else 1 if case in ("C06", "C07", "C08", "C09") else 0
    assert raw["process"]["exit_code"] == expected_exit
    before_preferences = snapshot(raw, "before", "preferences")
    after_preferences = snapshot(raw, "after", "preferences")
    if expected_exit:
        assert raw["observation"] is None
        assert after_preferences == before_preferences
        if case in ("C06", "C07"): assert b"--workspace" in stderr
        elif case == "C08": assert b"duplicate_path" in stderr
        elif case == "C09": assert b"missing.pgm" in stderr
        recheck()
        return
    assert raw["observation"] is not None
    observed = json_file(pin(raw["observation"]))
    assert observed["schema_version"] == 1 and observed["context"] == context
    assert observed["observation_kind"] == "normal_launcher"
    native_runtime(observed, binding, raw, value)
    startup = observed["startup"]
    assert startup["schema_version"] == 1 and startup["attempted"] is True and startup["completed"] is True
    assert startup["preferences_write_path"] == raw["files_before"]["preferences"]["source_path"]
    assert observed["preferences"]["values"]["shortcut_bindings"] == ["analysis.measure=Ctrl+K"]
    assert observed["preferences"]["extras"]["vendor"] == {"opaque": [True, None, 17]}
    assert observed["preferences"]["vendor_root"] == {"retained": True}
    if case in ("C01", "C05", "C10"):
        assert observed["document_infos"] == [] and startup["document_ids"] == [] and startup["active_index"] is None
        assert observed["results"] == [] and observed["workflow"] == []
        assert startup["source"] == "empty" and startup["paths"] == []
    elif case in ("C02", "C04", "C11", "C12"):
        loaded_workspace(observed, changed=case == "C11")
        assert startup["source"] == ("explicit_workspace" if case == "C04" else "persisted_workspace")
        assert startup["paths"] == [raw["files_before"]["workspace"]["source_path"]]
    elif case == "C03":
        docs = observed["document_infos"]
        assert len(docs) == 1 and docs[0]["shape"] == [1, 1, 1, 6, 8, 1]
        assert docs[0]["planes"] == [[3+y+x for y in range(6) for x in range(8)]]
        assert startup["source"] == "explicit_paths" and startup["active_index"] == 0
        assert startup["paths"] == [raw["files_before"]["first"]["source_path"]]
        assert observed["results"] == [] and observed["workflow"] == []
    if case == "C01":
        assert "startup_mode" not in decode(before_preferences)["values"]
        assert observed["preferences"]["extras"]["recent"] == ["must-not-open.pgm"]
    if case == "C02":
        assert decode(before_preferences)["values"]["startup_workspace"] == "../workspace.ijx"
        assert "\u914d\u7f6e \u03b1" in startup["preferences_write_path"]
    if case == "C10":
        assert observed["preferences_write_blocked"] is True and observed["preference_warnings"]
        assert observed["preferences"]["values"]["startup_mode"] == "empty"
        assert observed["preferences"]["values"]["startup_workspace"] == ""
        assert before_preferences == after_preferences
    if case == "C12":
        assert startup["reset_layout"] is True and startup["layout"]["saved_applied"] is False
        assert startup["layout"]["loaded_bytes"] == 0
    recheck()
