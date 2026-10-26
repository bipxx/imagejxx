"""Bounded installed paired capture; no expected values or acceptance.

Requires explicit passed source/product bindings. Parent imports no imagejxx.
Child uses the actual approved CPython engine and only selected installed sites.
"""
from __future__ import annotations

import argparse
import _thread
from contextlib import contextmanager
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import sys
import threading
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
MIB = 1024 * 1024
CONTEXTS = ("imgui_cpp", "imgui_python")
COMMON = {
    "observation_transport": "tests/python/actual_observation_transport.py",
    "observation_transport_header": "src/desktop/actual_observation_transport.hpp",
    "window": "src/desktop/window.cpp",
    "headless_header": "src/desktop/headless_session_internal.hpp",
    "settings_panel": "src/desktop/settings_panel.cpp",
    "settings_panel_header": "src/desktop/settings_panel.hpp",
    "native_settings": "src/native/settings.cpp",
    "native_preferences": "src/native/preferences.cpp",
    "desktop_binding": "src/desktop/python.cpp",
    "python_wrapper": "src/imagejxx/ui/app.py",
    "commands": "docs/parity/commands.json",
    "python_launcher": "scripts/capture_shortcuts_history_ui.py",
    "process_helper": "scripts/capture_windows_platform_workload.py",
    "python_package": "src/imagejxx/__init__.py",
    "python_preferences": "src/imagejxx/ui/persistence.py",
}
SPEC = {
    "shortcuts": {"stem": "shortcuts", "cases": tuple(f"K{i:02}" for i in range(1, 11)),
                  "flag": "--shortcuts-ui-case", "method": "_shortcuts_ui_case",
                  "schema": "imagejxx.shortcuts-ui-observations",
                  "roles": {"producer": "src/desktop/shortcuts_ui_probe.cpp",
                            "producer_header": "src/desktop/shortcuts_ui_probe.hpp",
                            "input": "src/desktop/shortcut_input.cpp",
                            "input_header": "src/desktop/shortcut_input.hpp",
                            "editor": "src/desktop/shortcut_editor.cpp",
                            "editor_header": "src/desktop/shortcut_editor.hpp",
                            "native_core": "src/native/shortcuts.cpp",
                            "native_header": "include/imagejxx/shortcuts.hpp",
                            "reader": "tests/python/test_shortcuts_ui.py",
                            "adoption": "docs/parity/reviews/G012-shortcuts-adoption-001.json"}},
    "history": {"stem": "history-settings", "cases": tuple(f"HU{i:02}" for i in range(1, 5)),
                "flag": "--history-settings-ui-case", "method": "_history_settings_ui_case",
                "schema": "imagejxx.history-settings-ui-observations",
                "roles": {"producer": "src/desktop/history_settings_ui_probe.cpp",
                          "producer_header": "src/desktop/history_settings_ui_probe.hpp",
                          "process_helper": "scripts/ci_owned_process.py",
                          "mac_process_helper": "scripts/ci_mac_process.py",
                          "document": "src/native/document.cpp",
                          "document_header": "include/imagejxx/document.hpp",
                          "application": "src/native/application.cpp",
                          "application_header": "include/imagejxx/application.hpp",
                          "session": "src/native/session.cpp",
                          "session_header": "include/imagejxx/session.hpp",
                          "reader": "tests/python/test_history_settings_ui.py",
                          "adoption": "docs/parity/reviews/G010-history-settings-ownership-001.json"}},
}
# Extra missing assertions use fresh cases and their own reader envelope. The
# original shortcut/history memberships and raw observation contract stay fixed.
SPEC["shortcuts-supplement"] = {
    **SPEC["shortcuts"],
    "cases": tuple(f"K{i:02}" for i in range(11, 15)),
    "reader_kind": "p06h.shortcuts-ui-supplement-reader-inputs.001",
    "roles": {**SPEC["shortcuts"]["roles"],
              "reader": "tests/python/test_shortcuts_ui_supplement.py",
              "producer_policy": "src/desktop/shortcut_policy_observations.cpp",
              "producer_policy_header": "src/desktop/shortcut_policy_observations.hpp"},
}
SPEC["scientific-orientation"] = {
    "stem": "scientific-orientation", "prefix": "p06i",
    "cases": tuple(f"O{i:02}" for i in range(1, 13)),
    "development_cases": tuple(f"O{i:02}" for i in range(1, 7)),
    "flag": "--scientific-orientation-ui-case", "method": "_scientific_orientation_ui_case",
    "schema": "imagejxx.scientific-orientation-ui-observations",
    "contract_field": "contract_id", "contract": "p06i.scientific-orientation-ui.001",
    "raw_name": "case.json", "index_name": "index.json",
    "reader_kind": "p06i.scientific-orientation-ui-reader-inputs.001",
    "build_kind": "p06i.scientific-orientation-source-product-binding.001",
    "python_origin_kind": "p06i.scientific-orientation-python-launcher-origin.001",
    "reader_env": "IMAGEJXX_SCIENTIFIC_ORIENTATION_UI_CASE_ROOT",
    "limits": {"parent_seconds": 120, "context_seconds": 60,
               "paired_raw_bytes": 24 * 32 * MIB, "native_archive_bytes": 32 * MIB,
               "live_documents": 16, "stage_bytes": 1024 * MIB},
    "roles": {"producer": "src/desktop/scientific_orientation_ui_probe.cpp",
              "producer_header": "src/desktop/scientific_orientation_ui_probe.hpp",
              "window_header": "src/desktop/window.hpp",
              "native_orientation": "src/native/axis_orientation_command.cpp",
              "native_orientation_header": "include/imagejxx/axis_orientation_command.hpp",
              "native_execution": "src/native/execution.cpp",
              "native_execution_header": "include/imagejxx/execution.hpp",
              "record_core": "src/native/axis_command_record.cpp",
              "record_core_header": "src/native/axis_command_record.hpp",
              "reader": "tests/python/test_scientific_orientation_ui.py"},
}
SPEC["appearance"] = {
    "stem": "settings-appearance", "prefix": "c2",
    "cases": ("appearance-persist", "appearance-reopen"),
    "flag": "--settings-appearance-ui-case", "method": "_settings_appearance_ui_case",
    "schema": "c2.settings-appearance.ui.001",
    "contract_field": "schema", "contract": "c2.settings-appearance.ui.001",
    "state_field": "state", "raw_name": "case.json", "paired_preferences": True,
    "reader_kind": "c2.settings-appearance-ui-reader-inputs.001",
    "build_kind": "c2.settings-appearance-source-product-binding.001",
    "python_origin_kind": "c2.settings-appearance-python-launcher-origin.001",
    "limits": {"parent_seconds": 300, "case_process_seconds": 60,
               "producer_seconds": 60, "frames": 750, "paired_raw_bytes": 64 * MIB,
               "stage_bytes": 192 * MIB},
    "roles": {"producer": "src/desktop/settings_appearance_ui_probe.cpp",
              "producer_header": "src/desktop/settings_appearance_ui_probe.hpp",
              "window_header": "src/desktop/window.hpp",
              "orthogonal": "src/desktop/orthogonal.cpp",
              "orthogonal_header": "src/desktop/orthogonal.hpp",
              "planar_annotation_panel": "src/desktop/planar_annotation_panel.cpp",
              "planar_annotation_panel_header": "src/desktop/planar_annotation_panel.hpp",
              "path_authoring_controls": "src/desktop/path_authoring_controls.cpp",
              "path_authoring_ui_probe": "src/desktop/path_authoring_ui_probe.cpp",
              "path_authoring_edge_ui_probe": "src/desktop/path_authoring_edge_ui_probe.cpp",
              "path_segment_ui_probe": "src/desktop/path_segment_ui_probe.cpp",
              "rendering": "src/desktop/rendering.cpp",
              "settings_io": "src/desktop/settings_io.cpp",
              "process_helper": "scripts/ci_owned_process.py",
              "mac_process_helper": "scripts/ci_mac_process.py",
              "reader": "tests/python/test_settings_appearance_ui.py",
              "reader_core": "tests/python/settings_appearance_reader.py",
              "expectations": "tests/python/settings_appearance_expectations.py"},
}
SPEC["drawing"] = {
    "stem": "settings-drawing", "prefix": "c3",
    "cases": ("drawing-persist", "drawing-reopen"),
    "flag": "--settings-drawing-ui-case", "method": "_settings_drawing_ui_case",
    "schema": "c3.settings-drawing.ui.001",
    "contract_field": "schema", "contract": "c3.settings-drawing.ui.001",
    "state_field": "state", "raw_name": "case.json", "paired_preferences": True,
    "explicit_preferences_argument": True,
    "preferences_seed": {"format": "imagejxx-preferences", "version": 1,
                         "values": {"c3_future_value": {"raw": [1, "two", True]}},
                         "c3_future_root": {"opaque": ["retain", 17]},
                         "extras": {"c3": "retain extra"}},
    "reader_kind": "c3.settings-drawing-ui-reader-inputs.001",
    "build_kind": "c3.settings-drawing-source-product-binding.001",
    "python_origin_kind": "c3.settings-drawing-python-launcher-origin.001",
    "limits": {"parent_seconds": 300, "case_process_seconds": 60,
               "producer_seconds": 60, "frames": 750, "paired_raw_bytes": 64 * MIB,
               "stage_bytes": 192 * MIB},
    "roles": {"producer": "src/desktop/settings_drawing_ui_probe.cpp",
              "producer_header": "src/desktop/settings_drawing_ui_probe.hpp",
              "window_header": "src/desktop/window.hpp",
              "selection_panel": "src/desktop/selection_panel.cpp",
              "native_selection_edit": "src/native/selection_edit.cpp",
              "native_roi_style": "src/native/roi_style.cpp",
              "native_transforms": "src/native/transforms.cpp",
              "native_document": "src/native/document.cpp",
              "native_operations": "src/native/operations.cpp",
              "operation_kernel": "src/native/operation_kernel.inc",
              "rendering": "src/desktop/rendering.cpp",
              "settings_io": "src/desktop/settings_io.cpp",
              "process_helper": "scripts/ci_owned_process.py",
              "mac_process_helper": "scripts/ci_mac_process.py",
              "reader": "tests/python/test_settings_drawing_ui.py",
              "reader_core": "tests/python/settings_drawing_reader.py",
              "expectations": "tests/python/settings_drawing_expectations.py",
              "rounded_reference": "tests/native/rounded_path_reference.inc"},
}
ORIENTATION_CONTRACT = {
    "path": str(ROOT / "build/scientific-orientation-entry001/contract.json"),
    "bytes": 30672,
    "sha256": "930f20fa1215eafaff0591c0ad419b87c28ccc085fde4ba0a4f9086cda963a51",
}
MODULES = ("imagejxx", "imagejxx._native", "imagejxx._desktop",
           "imagejxx.ui.app", "imagejxx.ui.persistence")
RUNTIME_ROLES = ("executable", "producer_artifact", "native_artifact")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def portable_kind(kind):
    return kind in ("history", "appearance", "drawing")


def read_json(path, limit=2 * MIB):
    def pairs(rows):
        result = {}
        for key, value in rows:
            require(key not in result, "Duplicate JSON key: " + key)
            result[key] = value
        return result
    require(0 < path.stat().st_size <= limit, "JSON byte bound: " + str(path))
    with path.open("rb") as stream:
        raw = stream.read(limit + 1)
    require(len(raw) <= limit, "JSON grew beyond bound")
    return json.loads(raw, object_pairs_hook=pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))


def pin(path, allow_empty=False):
    path = Path(path).absolute()
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode) and not path.is_symlink() and
            not getattr(info, "st_file_attributes", 0) & 0x400, "Nonregular/reparse input: " + str(path))
    require((0 if allow_empty else 1) <= info.st_size <= 256 * MIB, "Input file byte bound")
    digest, count = hashlib.sha256(), 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(MIB), b""):
            count += len(block)
            require(count <= info.st_size, "Pinned file grew")
            digest.update(block)
    after = path.stat()
    require(count == info.st_size and (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns) ==
            (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns), "Pinned file changed")
    return {"path": str(path.resolve(strict=True)), "bytes": count, "sha256": digest.hexdigest()}


def verify(value):
    require(type(value) is dict and {"path", "bytes", "sha256"} <= set(value), "Missing explicit pin")
    require(type(value["bytes"]) is int and value["bytes"] > 0 and
            type(value["sha256"]) is str and len(value["sha256"]) == 64, "Invalid explicit pin")
    actual = pin(Path(value["path"]))
    require(actual == {key: value[key] for key in ("path", "bytes", "sha256")}, "Pinned identity differs: " + str(value["path"]))
    return actual


def write(path, value):
    raw = (json.dumps(value, ensure_ascii=False, allow_nan=False, indent=2) + "\n").encode("utf-8")
    require(len(raw) <= 2 * MIB, "Capture metadata exceeds2MiB")
    with path.open("xb") as stream:
        stream.write(raw)


def engine(portable=False):
    if os.name != "nt" and portable:
        if sys.platform == "linux":
            return Path("/proc/self/exe").resolve(strict=True)
        require(sys.platform == "darwin", "Unsupported installed history capture platform")
        function = ctypes.CDLL(None)._NSGetExecutablePath
        function.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
        function.restype = ctypes.c_int
        size = ctypes.c_uint32(32768)
        buffer = ctypes.create_string_buffer(size.value)
        require(function(buffer, ctypes.byref(size)) == 0 and buffer.value,
                "Cannot observe actual CPython executable")
        return Path(os.fsdecode(buffer.value)).resolve(strict=True)
    require(os.name == "nt", "Windows-only installed capture; no fallback")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    function = kernel.GetModuleFileNameW
    function.argtypes, function.restype = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint32], ctypes.c_uint32
    buffer = ctypes.create_unicode_buffer(32768)
    size = function(None, buffer, len(buffer))
    require(0 < size < len(buffer), "Cannot observe actual CPython executable")
    return Path(buffer.value).resolve(strict=True)


def storage(root, limits=None, *, live=False):
    limits = {} if limits is None else limits
    byte_limit = limits.get("stage_bytes", 128 * MIB)
    size = files = entries = logs = 0
    pending = [root]
    while pending:
        with os.scandir(pending.pop()) as iterator:
            for entry in iterator:
                entries += 1
                require(entries <= 16384, "Owned output entry/reparse bound")
                # Directory enumeration can lag writes through an open Windows handle.
                try:
                    info = (os.stat(entry.path, follow_symlinks=False) if live else
                            entry.stat(follow_symlinks=False))
                except FileNotFoundError:
                    # Owned receipts are atomically renamed from *.writing.
                    # A live enumeration may outlast that temporary name. The
                    # mandatory post-owner scan remains strict and counts the
                    # final receipt; permission and other errors stay fatal.
                    if not live:
                        raise
                    continue
                require(entries <= 16384 and not entry.is_symlink() and
                        not getattr(info, "st_file_attributes", 0) & 0x400, "Owned output entry/reparse bound")
                if stat.S_ISDIR(info.st_mode):
                    pending.append(Path(entry.path))
                else:
                    require(stat.S_ISREG(info.st_mode), "Nonregular owned output")
                    size += info.st_size
                    files += 1
                    if entry.name.endswith((".stdout", ".stderr")):
                        logs += info.st_size
                require(size <= byte_limit and files <= 8192 and logs <= 16 * MIB,
                        "Owned output exceeds byte/file/log bounds")
    return {"bytes": size, "files": files, "log_bytes": logs}


def history_output(path):
    """Fresh workspace scratch without the retired local build-root assumption."""
    path = Path(path).absolute()
    for ancestor in (*reversed(path.parents), path):
        try:
            info = ancestor.lstat()
        except FileNotFoundError:
            continue
        require(not stat.S_ISLNK(info.st_mode) and
                not getattr(info, "st_file_attributes", 0) & 0x400,
                "History output has a linked/reparse ancestor")
    path = path.resolve()
    root = ROOT.resolve(strict=True)
    require(path != root and path.is_relative_to(root) and path.parent.is_dir() and
            not path.exists(), "Fresh history output must be an owned workspace child")
    return path


def history_owner():
    name = "ci_mac_process" if sys.platform == "darwin" else "ci_owned_process"
    path = ROOT / "scripts" / (name + ".py")
    spec = importlib.util.spec_from_file_location("_history_capture_" + name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.run_mac_owned if sys.platform == "darwin" else module.run_owned


@contextmanager
def history_storage_watch(output, limits, row):
    """Monitor only owned history scratch; existing owners perform cleanup."""
    require(threading.current_thread() is threading.main_thread(),
            "History ownership must run on the main thread")
    stopped = threading.Event()
    monitor = {"poll_seconds": .05, "checks": 0, "state": "running", "first_violation": None}
    row["live_storage_monitor"] = monitor

    def watch():
        while not stopped.wait(.05):
            try:
                storage(output, limits, live=True)
                monitor["checks"] += 1
            except BaseException as error:
                monitor["first_violation"] = {"type": type(error).__name__, "message": str(error)}
                if not stopped.is_set():
                    # Interrupt the owner, never signal a sampled product PID.
                    _thread.interrupt_main()
                return

    worker = threading.Thread(target=watch, name="history-storage-monitor", daemon=True)
    worker.start()
    try:
        yield
    finally:
        stopped.set()
        join_deadline = time.monotonic() + 10.
        interrupted = None
        try:
            worker.join(10.)
        except KeyboardInterrupt as error:
            # An already queued monitor interruption must not skip joining it.
            interrupted = error
            worker.join(max(0., join_deadline - time.monotonic()))
        monitor["state"] = "unresolved" if worker.is_alive() else (
            "failed" if monitor["first_violation"] is not None else "stopped")
        require(not worker.is_alive(), "History storage monitor did not stop within10s")
        if interrupted is not None:
            raise interrupted
    require(monitor["first_violation"] is None,
            "Live history storage guard refused: " + str(monitor["first_violation"]))


def run_history_owned(owner, argv, output, label, deadline, report):
    """Current ownership, retaining the original history process/report bounds.

    The existing storage guard runs before launch, every50ms while ownership is
    active, and after resolved cleanup. The native producer retains its own
    frame/time/report caps. Polling is not an OS disk quota. Every terminal
    ownership receipt remains a separate file.
    """
    started = time.monotonic()
    remaining = min(report["limits"].get("case_process_seconds", 15.), deadline - started)
    require(remaining > 0, "Case/context/parent deadline exhausted")
    row = {"argv": argv, "timeout_seconds": remaining, "state": "starting"}
    report["processes"].append(row)
    environment = os.environ.copy()
    for name in ("PYTHONPATH", "PYTHONHOME", "IMAGEJXX_PREFERENCES", "IMAGEJXX_CONFIG_DIR"):
        environment.pop(name, None)
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    process_path = output / (label + ".process.json")
    stdout_path, stderr_path = output / (label + ".stdout"), output / (label + ".stderr")
    try:
        storage(output, report["limits"])
        with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
            with history_storage_watch(output, report["limits"], row):
                terminal = owner(argv, cwd=output, env=environment, timeout=remaining,
                                 stdout=stdout, stderr=stderr, receipt_path=process_path)
        require(read_json(process_path) == terminal, "Owned terminal receipt differs from returned result")
        require(terminal.get("schema") == "imagejxx.owned-command.001" and
                terminal.get("state") == "exited" and type(terminal.get("exit_code")) is int and
                terminal["exit_code"] == 0 and
                terminal.get("target_completed") is True and terminal.get("cleanup_complete") is True and
                terminal.get("timed_out") is False and type(terminal.get("target_pid")) is int and
                terminal["target_pid"] > 0, "History product lacks a successful resolved owned exit")
        row.update(pid=terminal["target_pid"], exit_code=terminal["exit_code"], state="passed")
        storage(output, report["limits"])
    except BaseException as error:
        row.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        row["elapsed_seconds"] = time.monotonic() - started
        if process_path.exists():
            row["owned_process_receipt"] = pin(process_path)
        if stdout_path.exists():
            row["stdout"] = pin(stdout_path, True)
        if stderr_path.exists():
            row["stderr"] = pin(stderr_path, True)
    return row


def run_owned(api, job_class, argv, output, label, deadline, report, case_directory=None):
    started = time.monotonic()
    remaining = min(15., deadline - started)
    require(remaining > 0, "Case/context/parent deadline exhausted")
    row = {"argv": argv, "timeout_seconds": remaining, "state": "starting"}
    report["processes"].append(row)
    environment = os.environ.copy()
    for name in ("PYTHONPATH", "PYTHONHOME", "IMAGEJXX_PREFERENCES", "IMAGEJXX_CONFIG_DIR"):
        environment.pop(name, None)
    process = None
    try:
        with (output / (label + ".stdout")).open("xb") as stdout, (output / (label + ".stderr")).open("xb") as stderr:
            process = job_class(api, argv, output, environment, stdout, stderr)
            row.update(pid=process.pid, creation_filetime=process.creation, state="running")
            while True:
                storage(output, report["limits"])
                if case_directory is not None and case_directory.exists():
                    storage(case_directory, {"stage_bytes": report["limits"]["native_archive_bytes"]})
                require(time.monotonic() - started <= remaining, "Case/aggregate process deadline exceeded")
                code = process.exit_code()
                if code != 259:
                    row["exit_code"] = code
                    require(code == 0, "Owned product exited " + str(code))
                    break
                time.sleep(.05)
            row["state"] = "passed"
    except BaseException as error:
        row.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        cleanup = time.monotonic() + 10
        if process is not None:
            try:
                row["owned_job_pids_before_cleanup"] = sorted(process.pids())
                if process.pids():
                    require(api.kernel.TerminateJobObject(process.job, 1), "Cannot terminate owned job")
                while process.pids():
                    require(time.monotonic() <= cleanup, "Owned job cleanup exceeds10s")
                    time.sleep(.01)
                row["owned_job_pids_after_cleanup"] = sorted(process.pids())
            except BaseException as error:
                row.update(state="failed", cleanup_error={"type": type(error).__name__, "message": str(error)})
                raise
            finally:
                process.close()
        row["elapsed_seconds"] = time.monotonic() - started
        row["stdout"] = pin(output / (label + ".stdout"), True)
        row["stderr"] = pin(output / (label + ".stderr"), True)
    return row


def python_case(args):
    launch = read_json(args.launch_inputs)
    site = Path(launch["python_site_packages"]).resolve(strict=True)
    numpy_site = Path(launch["numpy"]["site_packages"]).resolve(strict=True)
    # This absent fresh cache prefix plus dont_write_bytecode prevents reuse or
    # creation of unbound package bytecode outside the retained capture.
    sys.pycache_prefix = str(args.directory.parent / "unused-bytecode-cache")
    # -I removes environment/user-site injection. Remove any remaining engine
    # site path, then add only the two explicit approved installed locations.
    sys.path[:] = [str(site), str(numpy_site)] + [value for value in sys.path
        if "site-packages" not in Path(value).parts and "dist-packages" not in Path(value).parts]
    import imagejxx
    import imagejxx._native
    import imagejxx._desktop
    import imagejxx.ui.app
    import imagejxx.ui.persistence
    import numpy
    modules = {name: pin(Path(sys.modules[name].__file__)) for name in MODULES}
    numpy_origin = pin(Path(numpy.__file__))
    actual_engine = pin(engine(portable=portable_kind(args.kind)))
    require(modules == launch["installed_modules"] and actual_engine == launch["python_engine"], "Wrong installed Python/engine origins")
    require(numpy_origin == launch["numpy"]["module"] and numpy.__version__ == launch["numpy"]["version"] == "2.2.6", "Wrong approved NumPy")
    require(all(Path(value["path"]).is_relative_to(site) for value in modules.values()) and
            Path(numpy_origin["path"]).is_relative_to(numpy_site), "Module escapes selected installed sites")
    spec = SPEC[args.kind]
    origin = {"schema_version": 1, "kind": spec.get("python_origin_kind", "p06h." + spec["stem"] + "-python-launcher-origin.001"),
              "case_id": args.python_case, "context": "imgui_python", "process_id": os.getpid(),
              "source_set_sha256": launch["source_set_sha256"], "modules_before": modules,
              "numpy_before": numpy_origin, "engine_before": actual_engine, "observation_state": "running"}
    try:
        editor = imagejxx.ui.app.Editor(preferences=imagejxx.ui.persistence.Preferences(args.directory.parent / "preferences.json"))
        getattr(editor._editor, SPEC[args.kind]["method"])(args.python_case, str(args.directory))
        raw = read_json(args.directory / spec.get("raw_name", "report.json"), 16 * MIB)
        before, after = raw["runtime_before"], raw["runtime_after"]
        for current in (before, after):
            require(current["process_id"] == os.getpid() and current["executable"] == actual_engine and
                    current["producer_artifact"] == modules["imagejxx._desktop"] and
                    current["native_artifact"] == modules["imagejxx._desktop"], "Actual native-address module identity differs")
        origin["before"] = {role: before[role] for role in RUNTIME_ROLES}
        origin["after"] = {role: after[role] for role in RUNTIME_ROLES}
        require(origin["before"] == origin["after"], "Raw runtime origins changed")
        origin["observation_state"] = "observed"
    finally:
        origin["modules_after"] = {name: pin(Path(sys.modules[name].__file__)) for name in MODULES}
        origin["numpy_after"] = pin(Path(numpy.__file__))
        origin["engine_after"] = pin(engine(portable=portable_kind(args.kind)))
        if (origin["modules_before"] != origin["modules_after"] or
                origin["numpy_before"] != origin["numpy_after"] or
                origin["engine_before"] != origin["engine_after"]):
            origin["observation_state"] = "failed"
        origin_name = args.python_case + "-launcher-origin.json" if spec.get("paired_preferences") else "launcher-origin.json"
        write(args.directory.parent / origin_name, origin)
    require(origin["modules_before"] == origin["modules_after"] and
            origin["numpy_before"] == origin["numpy_after"] and
            origin["engine_before"] == origin["engine_after"], "Imported module bytes changed")


def capture(args):
    require(portable_kind(args.kind) or os.name == "nt", "Windows-only installed capture")
    spec = SPEC[args.kind]
    selected_cases = spec["cases"]
    if args.case_ids is not None:
        require("development_cases" in spec and tuple(args.case_ids) in
                (spec["development_cases"], spec["cases"]), "Only the canonical orientation development/full membership is supported")
        selected_cases = tuple(args.case_ids)
    prefix = spec.get("prefix", "p06h")
    limits = {"parent_seconds": 60, "case_process_seconds": 15, "producer_seconds": 15,
              "frames": 180, "raw_bytes": 16 * MIB, "paired_raw_bytes": 32 * MIB,
              "cleanup_seconds": 10, "stage_bytes": 128 * MIB, "stage_files": 8192, "log_bytes": 16 * MIB}
    limits.update(spec.get("limits", {}))
    started = time.monotonic()
    deadline = started + limits["parent_seconds"]
    if portable_kind(args.kind):
        output = history_output(args.output)
    else:
        output = args.output.absolute().resolve()
        build_root = (ROOT / "build").resolve()
        require(output != build_root and output.is_relative_to(build_root), "Fresh output must be a workspace build child")
    output.mkdir(exist_ok=False)
    sys.pycache_prefix = str(output / "unused-bytecode-cache")
    report = {"schema_version": 1, "kind": prefix + ".installed-ui-capture.001", "kind_selection": args.kind,
              "state": "running", "acceptance_claim": False, "processes": [], "cases": [],
              "limits": limits}
    selection = {}
    if "development_cases" in spec:
        selection = {"selected_case_ids": list(selected_cases),
                     "complete_case_coverage": selected_cases == spec["cases"]}
        report.update(selection)
    consumed = []
    try:
        bindings = read_json(args.product_bindings)
        require(bindings["schema_version"] == 1 and bindings["kind"] == prefix + ".installed-ui-capture-bindings.001" and
                bindings["kind_selection"] == args.kind, "Wrong explicit product binding kind")
        reference = {}
        if "development_cases" in spec:
            require(bindings["adopted_contract"] == ORIENTATION_CONTRACT, "Wrong adopted orientation contract reference")
            reference = {"adopted_contract": verify(bindings["adopted_contract"])}
            consumed.append(reference["adopted_contract"])
            report.update(reference)
        for name, path in (("build_receipt", args.build_receipt), ("source_freeze", args.source_freeze)):
            require(verify(bindings[name]) == pin(path), "Explicit input path/pin differs: " + name)
            consumed.append(bindings[name])
        consumed.append(pin(args.product_bindings))
        build, freeze = read_json(args.build_receipt), read_json(args.source_freeze)
        require(build["state"] == "passed" and build["source_freeze"] == bindings["source_freeze"] and
                build["built_products"] == bindings["built_products"], "Passed build does not bind these sources/products")
        require(freeze["schema_version"] == 1 and type(freeze["sources"]) is list and
                1 <= len(freeze["sources"]) <= 8192, "Invalid complete source freeze")
        frozen = {}
        for item in freeze["sources"]:
            logical = item["logical_path"]
            require(type(logical) is str and logical not in frozen and not Path(logical).is_absolute() and
                    ".." not in Path(logical).parts, "Invalid/duplicate frozen logical source")
            frozen[logical] = item
        roles = COMMON | spec["roles"]
        sources = []
        for role, logical in sorted(roles.items()):
            require(logical in frozen, "Missing explicit frozen role: " + logical)
            reference_commands = role == "commands" and "reader_contract_commands" in bindings
            if reference_commands:
                # This immutable premerge corpus owns the original ten frozen
                # reader contracts. It is reference input, not compiled source
                # or a replacement for the passed product's live source freeze.
                selected = bindings["reader_contract_commands"]
                require(selected == {
                    "path": str(ROOT / "build/shortcuts-001/settings-adoption001/baseline/docs/parity/commands.json"),
                    "bytes": 3895117,
                    "sha256": "2031220bc79a5f396378b2eac76e48cfbb7e4ac55546626b3719868f5adbacb7",
                }, "Wrong immutable original reader contract corpus")
                item = verify(selected)
            else:
                item = verify(frozen[logical])
                require(pin(ROOT / logical) == {**item, "path": str((ROOT / logical).resolve())}, "Active consumed source changed")
            destination = output / "source" / logical
            destination.parent.mkdir(parents=True, exist_ok=True)
            with Path(item["path"]).open("rb") as source, destination.open("xb") as target:
                target.write(source.read(item["bytes"] + 1))
            copied = pin(destination)
            require((copied["bytes"], copied["sha256"]) == (item["bytes"], item["sha256"]), "Source snapshot copy changed")
            sources.append({"role": role, "logical_path": logical, **copied})
            consumed.append(item)
            if not reference_commands:
                consumed.append(pin(ROOT / logical))
        canonical = [{key: row[key] for key in ("role", "logical_path", "bytes", "sha256")}
                     for row in sorted(sources, key=lambda row: row["role"])]
        source_set = hashlib.sha256(json.dumps(canonical, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        cpp_root, site = args.cpp_prefix.resolve(strict=True), args.python_site_packages.resolve(strict=True)
        require(cpp_root.is_dir() and site.is_dir(), "Installed roots must be directories")
        products = bindings["products"]
        require(set(products) == set(CONTEXTS) and set(bindings["installed_modules"]) == set(MODULES), "Product/module membership differs")
        for context in CONTEXTS:
            require(set(products[context]) == set(RUNTIME_ROLES), "Runtime product role membership differs")
            for role in RUNTIME_ROLES:
                product = verify(products[context][role])
                consumed.append(product)
                if context == "imgui_cpp" or role != "executable":
                    require(Path(product["path"]).is_relative_to(cpp_root if context == "imgui_cpp" else site), "Product escapes installed root")
        built = {name: verify(value) for name, value in bindings["built_products"].items()}
        require(set(built) == {"cpp", "_native", "_desktop"}, "Built product membership differs")
        consumed.extend(built.values())
        require(products["imgui_cpp"]["executable"] == products["imgui_cpp"]["producer_artifact"] ==
                products["imgui_cpp"]["native_artifact"], "Standalone actual module roles differ")
        for name in MODULES:
            installed = verify(bindings["installed_modules"][name])
            require(Path(installed["path"]).is_relative_to(site), "Installed module escapes selected site")
            consumed.append(installed)
        for selected, built_pin in ((products["imgui_cpp"]["executable"], built["cpp"]),
                                   (bindings["installed_modules"]["imagejxx._native"], built["_native"]),
                                   (bindings["installed_modules"]["imagejxx._desktop"], built["_desktop"])):
            require((selected["bytes"], selected["sha256"]) == (built_pin["bytes"], built_pin["sha256"]), "Installed compiled product differs from passed build")
        for name, logical in (("imagejxx", COMMON["python_package"]), ("imagejxx.ui.app", COMMON["python_wrapper"]),
                              ("imagejxx.ui.persistence", COMMON["python_preferences"])):
            require(all(bindings["installed_modules"][name][key] == frozen[logical][key] for key in ("bytes", "sha256")), "Installed Python source differs from freeze")
        python = verify(bindings["python_engine"])
        require(python == pin(engine(portable=portable_kind(args.kind))) and products["imgui_python"]["executable"] == python and
                products["imgui_python"]["producer_artifact"] == products["imgui_python"]["native_artifact"] ==
                bindings["installed_modules"]["imagejxx._desktop"], "Direct engine/actual desktop module binding differs")
        numpy = bindings["numpy"]
        numpy_site = args.numpy_site_packages.resolve(strict=True)
        require(numpy_site.is_dir() and numpy["version"] == "2.2.6" and
                Path(numpy["site_packages"]).resolve() == numpy_site and
                Path(numpy["module"]["path"]).is_relative_to(numpy_site), "Explicit NumPy site/version differs")
        consumed.extend((python, verify(numpy["module"])))
        launch = {"python_site_packages": str(site), "numpy": numpy, "python_engine": python,
                  "installed_modules": bindings["installed_modules"], "source_set_sha256": source_set}
        launch_path = output / "launch-inputs.json"
        write(launch_path, launch)
        if portable_kind(args.kind):
            owner = history_owner()
        else:
            sys.path.insert(0, str(ROOT / "scripts"))
            from capture_windows_platform_workload import Win32, JobProcess
            api = Win32(None)  # Process containment only; no OS input/hooks/capture.
        provenance = {"schema_version": 1, "kind": spec.get("reader_kind", "p06h." + spec["stem"] + "-ui-reader-inputs.001"),
                      "sources": sources, "source_set_sha256": source_set, "contexts": {}, "cases": [],
                      **selection, **reference}
        elapsed = {context: 0. for context in CONTEXTS}
        total_raw = 0
        for case in selected_cases:
            for context in CONTEXTS:
                paired = spec.get("paired_preferences", False)
                directory = output / context / case if paired else output / context / case / "run"
                directory.parent.mkdir(parents=True, exist_ok=paired and case != selected_cases[0])
                preferences_path = directory.parent / "preferences.json"
                if "preferences_seed" in spec and case == selected_cases[0]:
                    # Fixture input only, before constructing either real host.
                    # Subsequent changes belong exclusively to actual Apply.
                    require(not preferences_path.exists(), "Drawing preference seed must be fresh")
                    write(preferences_path, spec["preferences_seed"])
                if context == "imgui_cpp":
                    argv = [products[context]["executable"]["path"], spec["flag"], case, str(directory)]
                    if spec.get("explicit_preferences_argument"):
                        argv.append(str(preferences_path))
                else:
                    argv = [python["path"], "-I", "-X", "utf8", "-B", str(output / "source" / COMMON["python_launcher"]),
                            "--kind", args.kind, "--python-case", case, "--directory", str(directory), "--launch-inputs", str(launch_path)]
                case_deadline = deadline
                if "context_seconds" in limits:
                    remaining = limits["context_seconds"] - elapsed[context]
                    require(remaining > 0, "Actual context60s budget exhausted")
                    case_deadline = min(deadline, time.monotonic() + remaining)
                if portable_kind(args.kind):
                    process = run_history_owned(owner, argv, output, context + "-" + case,
                                                case_deadline, report)
                else:
                    process = run_owned(api, JobProcess, argv, output, context + "-" + case, case_deadline, report,
                                        directory if "native_archive_bytes" in limits else None)
                elapsed[context] += process["elapsed_seconds"]
                if "context_seconds" in limits:
                    require(elapsed[context] <= limits["context_seconds"], "Actual context elapsed exceeds60s")
                raw_path = directory / spec.get("raw_name", "report.json")
                raw = read_json(raw_path, 16 * MIB)  # Retain original bytes; no rewrite/semantic oracle.
                require(raw["schema"] == spec["schema"] and raw[spec.get("contract_field", "contract")] ==
                        spec.get("contract", "p06h." + spec["stem"] + "-ui.001") and
                        (raw["case_id"], raw["context"]) == (case, context) and raw[spec.get("state_field", "observation_state")] == "observed" and
                        raw["acceptance_claim"] is False and 1 <= len(raw["frames"]) <= limits["frames"] and 0 < raw["elapsed_seconds"] <= limits["producer_seconds"],
                        "Actual producer envelope/caps differ")
                require(raw["runtime_before"] == raw["runtime_after"] and raw["runtime_before"]["process_id"] == process["pid"], "Actual process/runtime identity differs")
                for role in RUNTIME_ROLES:
                    require(raw["runtime_before"][role] == products[context][role], "Actual loaded artifact differs: " + role)
                descriptor = {"id": case, "context": context, "raw": pin(raw_path)}
                total_raw += descriptor["raw"]["bytes"]
                if "index_name" in spec:
                    index_path = directory / spec["index_name"]
                    index = read_json(index_path, 128 * 1024)
                    require(index["schema_version"] == 1 and index["contract_id"] == spec["contract"] and
                            (index["case_id"], index["context"]) == (case, context) and
                            index["observation_state"] == "observed" and index["acceptance_claim"] is False and
                            index["report"] == {"member": spec["raw_name"], "bytes": descriptor["raw"]["bytes"],
                                                "sha256": descriptor["raw"]["sha256"]}, "Actual case index differs")
                    descriptor["case_index"] = pin(index_path)
                    require(descriptor["raw"]["bytes"] + descriptor["case_index"]["bytes"] <= limits["native_archive_bytes"],
                            "Actual native case archive exceeds32MiB")
                    require(raw["frame_count"] == len(raw["frames"]) and raw["limits"] == {
                        "frames_per_case": 180, "seconds_per_case": 15., "report_bytes_per_case": 16 * MIB,
                        "native_archive_bytes_per_case": 32 * MIB, "live_documents": 16},
                        "Actual orientation producer limits differ")
                    storage(directory, {"stage_bytes": limits["native_archive_bytes"]})
                if context == "imgui_python":
                    origin_path = directory.parent / (case + "-launcher-origin.json" if paired else "launcher-origin.json")
                    origin = read_json(origin_path, 128 * 1024)
                    require(origin["before"] == origin["after"] == products[context] and origin["process_id"] == process["pid"] and
                            origin["modules_before"] == origin["modules_after"] == bindings["installed_modules"], "Python launcher origins differ")
                    descriptor["launcher_origin"] = pin(origin_path)
                    total_raw += descriptor["launcher_origin"]["bytes"]
                require(total_raw <= limits["paired_raw_bytes"], "Paired raw/origin byte bound exceeded")
                provenance["cases"].append(descriptor)
                report["cases"].append(descriptor)
                storage(output, limits)
        for value in consumed:
            verify(value)
        require(time.monotonic() <= deadline, "Aggregate parent deadline exceeded")
        product_receipt = output / "source-product-binding.json"
        write(product_receipt, {"schema_version": 1, "kind": spec.get("build_kind", "p06h." + spec["stem"] + "-source-product-binding.001"),
              "source_set_sha256": source_set, "sources": sources, "products": products,
              "passed_build_receipt": pin(args.build_receipt), "source_freeze": pin(args.source_freeze),
              "explicit_product_bindings": pin(args.product_bindings), "acceptance_claim": False,
              **selection, **reference})
        for context in CONTEXTS:
            provenance["contexts"][context] = {"installed_root": str(cpp_root if context == "imgui_cpp" else site),
                **products[context], "build_receipt": pin(product_receipt), "source_set_sha256": source_set,
                "elapsed_seconds": elapsed[context]}
        write(output / "provenance.json", provenance)
        require(time.monotonic() <= deadline, "Aggregate parent deadline exceeded during metadata freeze")
        report.update(state="passed", provenance=pin(output / "provenance.json"), paired_raw_bytes=total_raw,
                      storage=storage(output, limits), inputs={"product_bindings": pin(args.product_bindings),
                      "build_receipt": pin(args.build_receipt), "source_freeze": pin(args.source_freeze)})
    except BaseException as error:
        report.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        report["elapsed_seconds"] = time.monotonic() - started
        late = report["state"] == "passed" and report["elapsed_seconds"] > limits["parent_seconds"]
        if late:
            report.update(state="failed", error={"type": "TimeoutError", "message": "Aggregate parent deadline exceeded"})
        write(output / "capture.json", report)
        if late:
            raise TimeoutError("Aggregate parent deadline exceeded")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=SPEC, required=True)
    parser.add_argument("--python-case")
    parser.add_argument("--case-ids", nargs="+", help="Canonical scientific-orientation O01..O06 development subset or all O01..O12")
    for name in ("directory", "launch-inputs", "output", "cpp-prefix", "python-site-packages",
                 "numpy-site-packages", "build-receipt", "source-freeze", "product-bindings"):
        parser.add_argument("--" + name, type=Path)
    args = parser.parse_args()
    if args.python_case:
        require(args.python_case in SPEC[args.kind]["cases"] and args.directory is not None and
                args.launch_inputs is not None and args.case_ids is None, "Explicit valid child case/directory/launch inputs required")
        args.directory = args.directory.absolute()
        python_case(args)
    else:
        require(all(getattr(args, name) is not None for name in ("output", "cpp_prefix", "python_site_packages",
                    "numpy_site_packages", "build_receipt", "source_freeze", "product_bindings")), "All explicit capture inputs are required")
        capture(args)


if __name__ == "__main__":
    main()
