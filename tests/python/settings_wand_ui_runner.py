"""Bounded fresh-process Python launch for the independent Wand UI reader."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import signal
import time


CASES = ("wand-consume", "wand-drafts", "wand-persist", "wand-reopen")


def terminate_process_tree(child, runtime_path):
    """Stop only this owned launch tree, including a Windows venv interpreter."""
    runtime, metadata_error = None, None
    try:
        if runtime_path.exists():
            if not 0 < runtime_path.stat().st_size <= 65536:
                raise ValueError("Wand runtime identity byte bound")
            runtime = json.loads(runtime_path.read_bytes())
            if (not isinstance(runtime, dict)
                    or any(type(runtime.get(key)) is not int or not 0 < runtime[key] < 2**32
                           for key in ("process_id", "parent_process_id"))
                    or (runtime["process_id"] != child.pid and runtime["parent_process_id"] != child.pid)):
                raise ValueError("Cannot bind Wand runtime metadata to the owned launcher")
    except (ValueError, OSError, UnicodeError) as error:
        # A partially written diagnostic must never prevent cleanup of the
        # already-owned Popen tree. Never use an unvalidated PID as a target.
        runtime, metadata_error = None, str(error)
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        kernel.WaitForSingleObject.restype = wintypes.DWORD
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel.CloseHandle.restype = wintypes.BOOL
        handle = None
        if runtime and runtime["process_id"] != child.pid:
            handle = kernel.OpenProcess(0x00100000, False, runtime["process_id"])
            if not handle and ctypes.get_last_error() != 87:
                metadata_error = "Cannot open the additional Wand interpreter exit handle: " + str(ctypes.get_last_error())
        try:
            command = [str(Path(os.environ["SystemRoot"]) / "System32/taskkill.exe"),
                       "/PID", str(child.pid), "/T", "/F"]
            result = subprocess.run(command, capture_output=True, text=True, timeout=5,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            child.wait(timeout=5)
            if handle and kernel.WaitForSingleObject(handle, 5000) != 0:
                raise RuntimeError("Timed-out Wand interpreter has not exited")
            return {"method": "owned taskkill tree plus bound process-handle waits",
                    "taskkill_exit_code": result.returncode, "launcher_reaped": True,
                    "known_interpreter_exited": bool(runtime) and metadata_error is None,
                    "runtime_metadata_error": metadata_error}
        finally:
            if handle:
                kernel.CloseHandle(handle)
    try:
        os.killpg(child.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    child.wait(timeout=5)
    return {"method": "owned session process-group SIGKILL", "launcher_reaped": True,
            "runtime_metadata_error": metadata_error}


def produce_python_reports(root):
    root = Path(root).resolve(strict=True)
    script = Path(__file__).with_name("settings_wand_ui_child.py").resolve(strict=True)
    dependencies = sorted({str(Path(value).resolve()) for value in sys.path
                           if value and Path(value).is_dir() and Path(value).name == "site-packages"})
    fixture = {
        "format": "imagejxx-preferences", "version": 1,
        "values": {"wand_tolerance": 0.0, "wand_connectivity": 4,
                   "cfg1_future": {"items": [True, None, "λ", 2**64 - 1]}},
        "extras": {"recent": [], "cfg1_extra": {"label": "μ", "values": [3, 1]}},
        "cfg1_root": {"note": "root", "uint": 2**64 - 1},
    }
    receipt = {"state": "running", "scope": "Fresh Python launch observations; independent readers follow",
               "acceptance_claim": False, "runs": [], "reports": {}}
    def save():
        (root / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    save()
    try:
        for case in CASES:
            paired = case in ("wand-persist", "wand-reopen")
            parent = root / ("persisted-pair" if paired else case)
            parent.mkdir(exist_ok=paired)
            directory = parent / ("persist_capture" if case == "wand-persist" else
                                  "reopen_capture" if case == "wand-reopen" else "capture")
            preferences = parent / "preferences.json"
            if case == "wand-persist":
                with preferences.open("x", encoding="utf-8") as stream:
                    json.dump(fixture, stream, ensure_ascii=False)
            before = preferences.read_bytes() if preferences.exists() else None
            argv = [sys.executable, "-I", "-X", "utf8", "-B", str(script), case,
                    str(directory), str(preferences), json.dumps(dependencies)]
            environment = os.environ.copy()
            for name in ("PYTHONHOME", "PYTHONPATH", "PYTHONUSERBASE", "VIRTUAL_ENV", "CONDA_PREFIX"):
                environment.pop(name, None)
            entry = {"case": case, "argv": argv, "state": "running",
                     "started": time.time(), "timeout_seconds": 60}
            receipt["runs"].append(entry)
            save()
            with (parent / (case + ".log")).open("xb") as log:
                child = subprocess.Popen(argv, cwd=root, env=environment, stdout=log,
                                         stderr=subprocess.STDOUT, start_new_session=os.name != "nt")
                entry["launcher_process_id"] = child.pid
                save()
                try:
                    code = child.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    entry["timeout_cleanup"] = terminate_process_tree(
                        child, parent / (case + ".runtime.json"))
                    entry.update(timed_out=True, exit_code=child.returncode)
                    save()
                    raise
            entry.update(exit_code=code, ended=time.time())
            if code:
                raise RuntimeError("Python Wand observation child failed: " + case)
            identity = json.loads((parent / (case + ".runtime.json")).read_bytes())
            if identity["process_id"] != child.pid and identity["parent_process_id"] != child.pid:
                raise ValueError("Python Wand runtime is not the launched child or its venv interpreter")
            entry["runtime"] = identity
            report = directory / "case.json"
            if not 0 < report.stat().st_size < 16 * 1024**2:
                raise ValueError("Python Wand observation exceeds its byte bound")
            row = json.loads(report.read_bytes())
            if (row.get("case_id") != case or row.get("context") != "imgui_python"
                    or row.get("status") != "observed" or row.get("acceptance_claim") is not False
                    or row["fixture"]["runtime"]["process_id"] != identity["process_id"]):
                raise ValueError("Python Wand observation identity mismatch")
            if case == "wand-reopen" and preferences.read_bytes() != before:
                raise ValueError("Fresh Python reopen changed the persisted bytes")
            entry.update(state="observed", report_sha256=hashlib.sha256(report.read_bytes()).hexdigest())
            receipt["reports"][case] = str(report)
            save()
        receipt["state"] = "observed"
        return receipt["reports"]
    except BaseException as error:
        receipt.update(state="failed", error=str(error))
        if receipt["runs"] and receipt["runs"][-1]["state"] != "observed":
            receipt["runs"][-1].update(state="failed", ended=time.time())
        raise
    finally:
        save()
