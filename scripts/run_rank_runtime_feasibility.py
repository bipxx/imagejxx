"""Two fixed Windows normal-GL rank feasibility launches; no acceptance verdict.

Source-only preparation until explicitly invoked by the integration owner.
Uses only the standard library. Never imports the product in the supervisor.
"""
from __future__ import annotations

import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import platform
import struct
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
CAP = 64 * 1024 * 1024
LOG_CAP = 512 * 1024
PLAN = {"schema": 1, "mode": "feasibility", "dtype": "uint8",
        "method": "minimum", "radius": 2.0}
WRAPPER = r'''
from pathlib import Path
import importlib.util,sys,json,os
root,native_path,desktop_path,plan_path,dest=map(Path,sys.argv[1:])
os.chdir(root);sys.path.insert(0,str(root/'src'))
spec=importlib.util.spec_from_file_location('imagejxx._native',native_path)
native=importlib.util.module_from_spec(spec);sys.modules['imagejxx._native']=native;spec.loader.exec_module(native)
import imagejxx as ij
spec=importlib.util.spec_from_file_location('imagejxx._desktop',desktop_path)
desktop=importlib.util.module_from_spec(spec);sys.modules['imagejxx._desktop']=desktop;spec.loader.exec_module(desktop);ij._desktop=desktop
assert Path(ij._native.__file__).resolve()==native_path.resolve()
assert Path(desktop.__file__).resolve()==desktop_path.resolve()
assert Path(ij.__file__).resolve()==(root/'src/imagejxx/__init__.py').resolve()
from imagejxx.ui.app import Editor
from imagejxx.ui.persistence import Preferences
editor=Editor(preferences=Preferences(dest.parent/'preferences.json'))
row=json.loads(editor._editor._rank_runtime_case(plan_path.read_text(encoding='utf-8'),str(dest)))
assert row['context']=='imgui_python' and row['acceptance'] is False
print(json.dumps({'context':'imgui_python','completion':row['completion']}),flush=True)
'''


def pin(path):
    path = Path(path)
    if not path.is_absolute():
        path = ROOT / path
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return {"path": str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path),
            "bytes": path.stat().st_size, "sha256": digest.hexdigest()}


def checked_pins(rows):
    actual = []
    for row in rows:
        value = pin(row["path"])
        if (value["bytes"], value["sha256"]) != (row["bytes"], row["sha256"]):
            raise RuntimeError("Pinned input/product changed: " + row["path"])
        actual.append(value)
    return actual


def read_json(path):
    path = Path(path)
    if not path.is_absolute():
        path = ROOT / path
    if path.stat().st_size > 16 * 1024 * 1024:
        raise RuntimeError("Oversized JSON input: " + str(path))
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path, value):
    data = (json.dumps(value, indent=2, allow_nan=False) + "\n").encode("utf-8")
    if len(data) > 8 * 1024 * 1024:
        raise RuntimeError("Supervisor JSON exceeds8MiB bound")
    with Path(path).open("xb") as stream:
        stream.write(data)


class MemoryCounters(C.Structure):
    _fields_ = [("cb", W.DWORD), ("PageFaultCount", W.DWORD)] + [
        (name, C.c_size_t) for name in ("PeakWorkingSetSize", "WorkingSetSize",
        "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
        "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


class ProcessEntry(C.Structure):
    _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD), ("th32ProcessID", W.DWORD),
        ("th32DefaultHeapID", C.c_size_t), ("th32ModuleID", W.DWORD), ("cntThreads", W.DWORD),
        ("th32ParentProcessID", W.DWORD), ("pcPriClassBase", W.LONG), ("dwFlags", W.DWORD),
        ("szExeFile", W.WCHAR * 260)]


class Win:
    def __init__(self):
        self.kernel = C.WinDLL("kernel32", use_last_error=True)
        self.psapi = C.WinDLL("psapi", use_last_error=True)
        self.psapi.GetProcessMemoryInfo.argtypes = [W.HANDLE, C.c_void_p, W.DWORD]
        self.psapi.GetProcessMemoryInfo.restype = W.BOOL
        self.kernel.GetProcessId.argtypes = [W.HANDLE]
        self.kernel.GetProcessId.restype = W.DWORD
        self.kernel.QueryPerformanceFrequency.argtypes = [C.POINTER(C.c_longlong)]
        self.kernel.QueryPerformanceFrequency.restype = W.BOOL
        self.kernel.QueryPerformanceCounter.argtypes = [C.POINTER(C.c_longlong)]
        self.kernel.QueryPerformanceCounter.restype = W.BOOL
        self.kernel.GetLogicalProcessorInformationEx.argtypes = [W.DWORD, C.c_void_p, C.POINTER(W.DWORD)]
        self.kernel.GetLogicalProcessorInformationEx.restype = W.BOOL
        self.kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
        self.kernel.OpenProcess.restype = W.HANDLE
        self.kernel.CloseHandle.argtypes = [W.HANDLE]
        self.kernel.CloseHandle.restype = W.BOOL
        self.kernel.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
        self.kernel.WaitForSingleObject.restype = W.DWORD
        self.kernel.GetProcessTimes.argtypes = [W.HANDLE] + [C.c_void_p] * 4
        self.kernel.GetProcessTimes.restype = W.BOOL
        self.kernel.CreateToolhelp32Snapshot.argtypes = [W.DWORD, W.DWORD]
        self.kernel.CreateToolhelp32Snapshot.restype = W.HANDLE
        self.kernel.Process32FirstW.argtypes = [W.HANDLE, C.POINTER(ProcessEntry)]
        self.kernel.Process32FirstW.restype = W.BOOL
        self.kernel.Process32NextW.argtypes = [W.HANDLE, C.POINTER(ProcessEntry)]
        self.kernel.Process32NextW.restype = W.BOOL
        self.kernel.GetSystemTimes.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p]
        self.kernel.GetSystemTimes.restype = W.BOOL
        self.kernel.GlobalMemoryStatusEx.argtypes = [C.c_void_p]
        self.kernel.GlobalMemoryStatusEx.restype = W.BOOL
        frequency = C.c_longlong()
        self.check(self.kernel.QueryPerformanceFrequency(C.byref(frequency)))
        if frequency.value <= 0:
            raise RuntimeError("QueryPerformanceFrequency did not return a positive actual frequency")
        self.qpc_frequency = int(frequency.value)

    def check(self, result):
        if not result:
            raise C.WinError(C.get_last_error())
        return result

    def qpc(self):
        value = C.c_longlong()
        self.check(self.kernel.QueryPerformanceCounter(C.byref(value)))
        if value.value < 0:
            raise RuntimeError("QueryPerformanceCounter returned a negative timestamp")
        return int(value.value)

    def clock_profile(self):
        result = {name: vars(time.get_clock_info(name)) for name in ("perf_counter", "monotonic")}
        return {"external_scheduling": "perf_counter/perf_counter_ns", "python_clock_info": result,
                "actual_qpc_frequency": self.qpc_frequency,
                "native_steady_clock": "retained as native provenance only; no Python/C++ epoch mapping"}

    def physical_cores(self):
        # RelationProcessorCore: one returned variable-size record per active
        # physical core. This is a read-only bounded local Windows API query.
        length = W.DWORD()
        ok = self.kernel.GetLogicalProcessorInformationEx(0, None, C.byref(length))
        error = C.get_last_error()
        if ok or error != 122 or not 0 < length.value <= 1024 * 1024:
            raise RuntimeError("Physical-core size query failed/bound exceeded: " + str(error))
        buffer = C.create_string_buffer(length.value)
        self.check(self.kernel.GetLogicalProcessorInformationEx(0, buffer, C.byref(length)))
        if not 0 < length.value <= len(buffer):
            raise RuntimeError("Physical-core result length invalid")
        data = buffer.raw[:length.value]; offset = 0; count = 0
        while offset < len(data):
            if count >= 65536 or offset + 8 > len(data):
                raise RuntimeError("Physical-core record count/header bound exceeded")
            relationship, size = struct.unpack_from("<II", data, offset)
            if relationship != 0 or size < 8 or offset + size > len(data):
                raise RuntimeError("Physical-core relationship/record size invalid")
            count += 1; offset += size
        if count < 1 or offset != len(data):
            raise RuntimeError("Physical-core query returned no complete records")
        return {"status": "observed", "count": count,
                "api": "GetLogicalProcessorInformationEx(RelationProcessorCore)",
                "returned_bytes": len(data), "record_count": count,
                "scope": "active physical cores reported by the local Windows processor topology API"}

    def creation_time(self, handle):
        creation, exit_time, kernel, user = (C.c_ulonglong() for _ in range(4))
        self.check(self.kernel.GetProcessTimes(handle, C.byref(creation), C.byref(exit_time),
                                              C.byref(kernel), C.byref(user)))
        return int(creation.value)

    def require_live(self, handle, pid):
        if self.kernel.GetProcessId(handle) != pid:
            raise RuntimeError("Retained handle/PID identity mismatch")
        state = self.kernel.WaitForSingleObject(handle, 0)
        if state != 258:  # WAIT_TIMEOUT is the observed live process state.
            raise RuntimeError("Process is not live during engine admission: " + str(pid) + ":" + str(state))

    def parent_chain(self, engine_handle, engine_pid, process):
        launcher_handle = int(process._handle)
        self.require_live(launcher_handle, process.pid)
        self.require_live(engine_handle, engine_pid)
        snapshot = self.kernel.CreateToolhelp32Snapshot(0x00000002, 0)  # TH32CS_SNAPPROCESS
        if snapshot == C.c_void_p(-1).value:
            raise C.WinError(C.get_last_error())
        parents = {}; entries = 0; opened = []
        try:
            entry = ProcessEntry(); entry.dwSize = C.sizeof(entry)
            self.check(self.kernel.Process32FirstW(snapshot, C.byref(entry)))
            while True:
                entries += 1
                if entries > 16384:
                    raise RuntimeError("Process snapshot exceeds16384-entry admission bound")
                pid = int(entry.th32ProcessID)
                parents[pid] = (int(entry.th32ParentProcessID), entry.szExeFile)
                if not self.kernel.Process32NextW(snapshot, C.byref(entry)):
                    error = C.get_last_error()
                    if error != 18:  # ERROR_NO_MORE_FILES
                        raise C.WinError(error)
                    break
            lineage = []; seen = set(); current = engine_pid; child_creation = None
            for depth in range(16):
                if current in seen or current not in parents:
                    raise RuntimeError("Engine ancestry cycle/missing process before retained launcher")
                seen.add(current)
                if current == engine_pid:
                    handle = engine_handle
                elif current == process.pid:
                    handle = launcher_handle
                else:
                    # Query-only ancestry observation; these handles never own
                    # cleanup and these PIDs are never used as kill targets.
                    handle = self.check(self.kernel.OpenProcess(0x00101000, False, current))
                    opened.append(handle)
                self.require_live(handle, current)
                creation = self.creation_time(handle)
                if child_creation is not None and creation > child_creation:
                    raise RuntimeError("Observed parent was created after its child; PID reuse suspected")
                parent, executable = parents[current]
                lineage.append({"pid": current, "parent_pid": parent, "exe_file": executable,
                                "creation_filetime_100ns": creation, "depth": depth})
                if current == process.pid:
                    self.require_live(launcher_handle, process.pid)
                    self.require_live(engine_handle, engine_pid)
                    return {"observed_perf_counter_ns": time.perf_counter_ns(), "snapshot_entries": entries,
                            "maximum_hops": 16, "engine_to_launcher": lineage,
                            "launcher_pid": process.pid, "same_as_launcher": engine_pid == process.pid}
                child_creation = creation
                current = parent
            raise RuntimeError("Engine ancestry exceeds16-hop admission bound")
        finally:
            for handle in reversed(opened):
                self.kernel.CloseHandle(handle)
            self.kernel.CloseHandle(snapshot)

    def admit_engine(self, pid, process):
        # QUERY_INFORMATION | VM_READ | SYNCHRONIZE, never a launcher handle
        # selected by executable name or a discovered descendant kill target.
        access = 0x00100410
        handle = self.check(self.kernel.OpenProcess(access, False, pid))
        try:
            lineage = self.parent_chain(handle, pid, process)
            return handle, {"process_id": pid, "handle_process_id": int(self.kernel.GetProcessId(handle)),
                            "open_process_access": access, "lineage": lineage}
        except BaseException:
            self.kernel.CloseHandle(handle)
            raise

    def sample(self, handle, pid):
        counters = MemoryCounters(); counters.cb = C.sizeof(counters)
        started = time.perf_counter_ns()
        qpc_before = self.qpc()
        ok = self.psapi.GetProcessMemoryInfo(handle, C.byref(counters), counters.cb)
        error = C.get_last_error() if not ok else None
        qpc_after = self.qpc()
        row = {"pid": pid, "perf_counter_ns": time.perf_counter_ns(),
               "call_started_ns": started, "qpc_before": qpc_before, "qpc_after": qpc_after,
               "qpc_frequency": self.qpc_frequency,
               "private_bytes": int(counters.PrivateUsage) if ok else None}
        if not ok:
            row["winerror"] = error
        return row


def host(win):
    import winreg
    class Memory(C.Structure):
        _fields_ = [("length", W.DWORD), ("load_percent", W.DWORD)] + [
            (name, C.c_ulonglong) for name in ("total_physical", "available_physical",
            "total_pagefile", "available_pagefile", "total_virtual", "available_virtual", "extended_virtual")]
    result = {"perf_counter_ns": time.perf_counter_ns(), "platform": platform.platform(),
              "windows_version": list(sys.getwindowsversion()), "logical_processors": os.cpu_count(),
              "physical_core_count": "unobserved; logical count is not physical core count"}
    result["clock_profile"] = win.clock_profile()
    try:
        result["physical_core_observation"] = win.physical_cores()
        result["physical_core_count"] = result["physical_core_observation"]["count"]
    except Exception as error:
        result["physical_core_observation"] = {"status": "unobserved", "error": repr(error)}
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
            result["cpu_name"] = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
        memory = Memory(); memory.length = C.sizeof(memory)
        win.check(win.kernel.GlobalMemoryStatusEx(C.byref(memory)))
        result["ram"] = {name: int(getattr(memory, name)) for name, _ in memory._fields_[1:]}
        pointer = C.c_void_p()
        power = C.WinDLL("powrprof", use_last_error=True)
        power.PowerGetActiveScheme.argtypes = [W.HANDLE, C.POINTER(C.c_void_p)]
        power.PowerGetActiveScheme.restype = W.DWORD
        code = power.PowerGetActiveScheme(None, C.byref(pointer))
        if code:
            result["power_error"] = int(code)
        else:
            import uuid
            result["active_power_scheme"] = str(uuid.UUID(bytes_le=C.string_at(pointer, 16)))
            win.kernel.LocalFree.argtypes = [C.c_void_p]
            win.kernel.LocalFree.restype = C.c_void_p
            win.kernel.LocalFree(pointer)
    except Exception as error:
        result["observation_error"] = repr(error)
    return result


def load_observation(win, deadline):
    user = C.WinDLL("user32", use_last_error=True)
    user.GetForegroundWindow.restype = W.HWND
    user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
    user.GetWindowThreadProcessId.restype = W.DWORD
    rows = []; end = min(time.perf_counter() + 5, deadline)
    while time.perf_counter() < end:
        idle = C.c_ulonglong(); kernel = C.c_ulonglong(); busy = C.c_ulonglong()
        ok = win.kernel.GetSystemTimes(C.byref(idle), C.byref(kernel), C.byref(busy))
        pid = W.DWORD(); user.GetWindowThreadProcessId(user.GetForegroundWindow(), C.byref(pid))
        rows.append({"perf_counter_ns": time.perf_counter_ns(), "idle_100ns": idle.value,
            "kernel_100ns": kernel.value, "user_100ns": busy.value,
            "foreground_pid": pid.value, "system_times_ok": bool(ok)})
        time.sleep(min(.1, max(0, end - time.perf_counter())))
    return {"requested_seconds": 5, "actual_samples": rows,
            "cpu_scope": "system cumulative CPU counters; kernel includes idle; no per-process load inference"}


def owned_bytes(output):
    total = 0
    for directory, dirs, files in os.walk(output, followlinks=False):
        for name in dirs + files:
            path = Path(directory) / name
            if path.is_symlink() or path.is_junction():
                raise RuntimeError("Unexpected link in fresh owned output")
        total += sum((Path(directory) / name).stat().st_size for name in files)
    return total


def join_widget_memory(samples, marker, report, engine_pid, frequency):
    """Join actual QPC brackets only; never infer an unsampled allocation peak."""
    result = {"acceptance": False, "qpc_frequency": frequency, "engine_pid": engine_pid,
        "maximum_baseline_age_ms": 20, "cross_process_ambiguity_ticks": 1,
        "scope": "sampled engine PrivateUsage delta; no allocation census, unsampled peak or performance verdict",
        "errors": [], "trials": [], "ineligible_trial_ids": []}
    valid_frequency = type(frequency) is int and frequency > 0
    if (not valid_frequency or marker.get("qpc_frequency") != frequency
        or type(marker.get("qpc_frequency")) is not int
        or report.get("qpc_frequency") != frequency or type(report.get("qpc_frequency")) is not int):
        result["errors"].append("Native marker/raw/external QPC frequencies are not positive matching actual integers")
    if (type(engine_pid) is not int or engine_pid <= 0
        or type(marker.get("process_id")) is not int or marker["process_id"] != engine_pid
        or type(report.get("process_id")) is not int or report["process_id"] != engine_pid):
        result["errors"].append("Native marker/raw/sample engine process identities do not match")
    previous_after = None
    for index, sample in enumerate(samples):
        before, after = sample.get("qpc_before"), sample.get("qpc_after")
        if (type(sample.get("pid")) is not int or sample["pid"] != engine_pid
            or type(sample.get("qpc_frequency")) is not int or sample["qpc_frequency"] != frequency
            or type(before) is not int or type(after) is not int or not 0 <= before <= after
            or (previous_after is not None and before < previous_after)
            or (sample.get("private_bytes") is not None
                and (type(sample["private_bytes"]) is not int or sample["private_bytes"] < 0))):
            result["errors"].append("Invalid sample identity/frequency/bracket/order/value at index " + str(index))
        if type(after) is int:
            previous_after = after
    trials = report.get("trials", [])
    if not isinstance(trials, list) or len(trials) > 64:
        result["errors"].append("Actual trial list missing or exceeds64-entry bound")
        result["status"] = "missing_invalid_trials"
        return result
    seen = set()
    for trial in trials:
        trial_id = trial.get("id")
        if not isinstance(trial_id, str) or not trial_id or trial_id in seen:
            result["errors"].append("Actual trial identity missing or duplicated")
            continue
        seen.add(trial_id)
        if trial.get("eligible") is not True:
            result["ineligible_trial_ids"].append(trial_id)
            continue
        row = {"trial_id": trial_id, "endpoint": trial.get("endpoint"),
            "required_initial_state": trial.get("required_initial_state"),
            "process_id": engine_pid, "qpc_frequency": frequency,
            "memory_window": trial.get("memory_window"), "acceptance": False,
            "baseline": None, "valid_window_sample_indices": [], "ambiguous_sample_indices": [],
            "overlap_sample_indices": [], "sample_error_indices": [],
            "peak_private_bytes": None, "delta_private_bytes": None}
        result["trials"].append(row)
        if result["errors"]:
            row["status"] = "missing_clock_process_or_sample_integrity"
            continue
        window = trial.get("memory_window")
        if not isinstance(window, dict) or not all(name in window for name in ("input_before", "input_after", "endpoint")):
            row["status"] = "missing_native_memory_window"
            continue
        stamps = [window[name] for name in ("input_before", "input_after", "endpoint")]
        if any(not isinstance(stamp, dict)
               or type(stamp.get("steady_ns")) is not int or stamp["steady_ns"] < 0
               or type(stamp.get("qpc_before")) is not int or type(stamp.get("qpc_after")) is not int
               or not 0 <= stamp["qpc_before"] <= stamp["qpc_after"]
               or type(stamp.get("qpc_frequency")) is not int or stamp["qpc_frequency"] != frequency
               or stamp.get("qpc_supported", True) is not True for stamp in stamps):
            row["status"] = "missing_invalid_native_clock_boundary"
            continue
        before, after, endpoint = stamps
        if not before["qpc_after"] <= after["qpc_before"] <= after["qpc_after"] <= endpoint["qpc_before"]:
            row["status"] = "missing_native_boundary_order"
            continue
        pre, post, end = before["qpc_before"], after["qpc_after"], endpoint["qpc_after"]
        baseline_candidates = [index for index, sample in enumerate(samples)
            if sample["private_bytes"] is not None and sample["qpc_after"] + 1 < pre]
        baseline_index = max(baseline_candidates, key=lambda index: (samples[index]["qpc_after"], index), default=None)
        interior_attempts = [index for index, sample in enumerate(samples)
            if sample["qpc_before"] > post + 1 and sample["qpc_after"] + 1 < end]
        valid = [index for index in interior_attempts if samples[index]["private_bytes"] is not None]
        row["valid_window_sample_indices"] = valid
        row["interior_attempt_count"] = len(interior_attempts)
        row["valid_window_sample_count"] = len(valid)
        row["ambiguous_sample_indices"] = [index for index, sample in enumerate(samples)
            if abs(sample["qpc_after"] - pre) <= 1 or abs(sample["qpc_before"] - post) <= 1
            or abs(sample["qpc_after"] - end) <= 1]
        row["overlap_sample_indices"] = [index for index, sample in enumerate(samples)
            if sample["qpc_after"] + 1 >= pre and sample["qpc_before"] <= end + 1
            and index not in interior_attempts]
        lower = samples[baseline_index]["qpc_after"] if baseline_index is not None else pre
        relevant = [index for index, sample in enumerate(samples)
            if sample["qpc_after"] >= lower and sample["qpc_before"] <= end + 1]
        row["sample_error_indices"] = [index for index in relevant if samples[index]["private_bytes"] is None]
        gaps = [samples[b]["qpc_before"] - samples[a]["qpc_before"] for a, b in zip(relevant, relevant[1:])]
        row["sample_gap_qpc_ticks"] = gaps
        row["sample_gap_ns_floor"] = [gap * 1000000000 // frequency for gap in gaps]
        row["maximum_gap_qpc_ticks"] = max(gaps, default=None)
        row["gaps_over20ms"] = sum(gap * 1000 > frequency * 20 for gap in gaps)
        row["ambiguous_sample_count"] = len(row["ambiguous_sample_indices"])
        row["overlap_sample_count"] = len(row["overlap_sample_indices"])
        row["sample_error_count"] = len(row["sample_error_indices"])
        if baseline_index is None:
            row["status"] = "missing_preinput_baseline"
            continue
        baseline = samples[baseline_index]
        age = pre - baseline["qpc_after"]
        row["baseline"] = {"sample_index": baseline_index, **baseline}
        row["baseline_age_qpc_ticks"] = age
        row["baseline_age_ns_floor"] = age * 1000000000 // frequency
        if age * 1000 > frequency * 20:
            row["status"] = "missing_fresh_preinput_baseline"
            continue
        if row["sample_error_indices"]:
            row["status"] = "missing_sample_errors"
            continue
        if not valid:
            row["status"] = "missing_postinput_interior_sample"
            continue
        peak_index = max([baseline_index, *valid], key=lambda index: samples[index]["private_bytes"])
        row["peak_sample_index"] = peak_index
        row["peak_private_bytes"] = samples[peak_index]["private_bytes"]
        row["delta_private_bytes"] = row["peak_private_bytes"] - baseline["private_bytes"]
        row["status"] = "observed_sampled_delta"
    result["status"] = ("missing_clock_process_or_sample_integrity" if result["errors"] else
                        "eligible_trial_windows_evaluated" if result["trials"] else "missing_no_eligible_trials")
    return result


def run_one(win, argv, folder, deadline, output, context):
    folder.mkdir()
    marker = folder / "run" / "process.json"
    row = {"argv": argv, "cwd": str(ROOT), "acceptance": False,
           "memory_preinput_baseline": "per eligible trial: latest successful QPC-separated sample no older than20ms",
           "memory_preinput_delta": "per-trial sampled status/value only; absent evidence remains missing",
           "memory_clock_join": "matching verified-engine/native/external QPC frequency and actual QPC brackets; no steady/Python epoch assumption",
           "clock_profile": win.clock_profile(),
           "memory_acceptance": False, "samples": [], "errors": [],
           "memory_admission": {"state": "awaiting_marker", "path": str(marker.relative_to(ROOT)),
                                "started_perf_counter_ns": time.perf_counter_ns(), "incomplete_reads": 0}}
    process = None; engine_handle = None; engine_pid = None
    readers = []; overflow = threading.Event()
    logs = {}; stop_reason = None; launch = time.perf_counter()
    def drain(pipe, name):
        kept = dropped = 0
        try:
            with (folder / name).open("xb") as target:
                for block in iter(lambda: pipe.read(4096), b""):
                    count = min(len(block), LOG_CAP - kept)
                    if count:
                        target.write(block[:count]); kept += count
                    dropped += len(block) - count
                    if dropped:
                        overflow.set()
        except Exception as error:
            row["errors"].append("Log reader " + name + ": " + repr(error)); overflow.set()
        finally:
            logs[name] = {"retained_bytes": kept, "dropped_bytes": dropped}
    try:
        process = subprocess.Popen(argv, cwd=ROOT, stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True,
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
        row["pid"] = process.pid; row["launcher_pid"] = process.pid
        row["pid_semantics"] = "retained owned Popen launcher; not the memory target unless marker/lineage admit the same PID"
        row["popen_perf_counter_ns"] = time.perf_counter_ns()
        if win.kernel.GetProcessId(int(process._handle)) != process.pid:
            raise RuntimeError("Actual Popen process handle/PID identity mismatch")
        row["ownership"] = "retained Popen launcher owns tree cleanup; dedicated marker-admitted native engine handle owns samples"
        for name, pipe in (("capture.stdout", process.stdout), ("capture.stderr", process.stderr)):
            thread = threading.Thread(target=drain, args=(pipe, name), daemon=True)
            thread.start(); readers.append(thread)
        next_sample = time.perf_counter(); next_size = next_sample
        while process.poll() is None:
            now = time.perf_counter()
            if now >= deadline or now - launch >= 45:
                stop_reason = "owned product external45s/context300s/parent600s deadline"; break
            if overflow.is_set():
                stop_reason = "owned stdout/stderr log cap"; break
            if now >= next_size:
                if owned_bytes(output) > CAP - 8 * 1024 * 1024:
                    stop_reason = "owned artifact cap reserve reached"; break
                next_size = now + .1
            if engine_handle is None and marker.is_file():
                if marker.stat().st_size > 4096:
                    raise RuntimeError("Native process marker exceeds4096-byte admission bound")
                data = marker.read_bytes()
                try:
                    value = json.loads(data.decode("utf-8-sig"))
                except (json.JSONDecodeError, UnicodeDecodeError) as error:
                    admission = row["memory_admission"]
                    admission["incomplete_reads"] += 1
                    admission["last_incomplete_read"] = repr(error)[:512]
                else:
                    if (type(value.get("schema")) is not int or value["schema"] != 1
                        or value.get("producer_id") != "p06f.rank.normal-loop.process.001"
                        or value.get("context") != context
                        or type(value.get("process_id")) is not int or not 0 < value["process_id"] <= 0xffffffff
                        or type(value.get("steady_ns")) is not int or value["steady_ns"] < 0
                        or type(value.get("qpc_frequency")) is not int or value["qpc_frequency"] != win.qpc_frequency
                        or value.get("qpc_supported", True) is not True):
                        raise RuntimeError("Native engine process marker schema/context/PID is invalid")
                    engine_pid = value["process_id"]
                    engine_handle, identity = win.admit_engine(engine_pid, process)
                    row["engine_pid"] = engine_pid; row["engine_identity"] = identity
                    row["process_marker"] = {"value": value, "pin": {
                        "path": str(marker.relative_to(ROOT)), "bytes": len(data),
                        "sha256": hashlib.sha256(data).hexdigest()}}
                    row["memory_admission"].update({"state": "admitted_native_engine",
                        "completed_perf_counter_ns": time.perf_counter_ns(),
                        "interval_scope": "samples begin after marker/handle/ancestry admission; baseline eligibility determined offline per actual native QPC widget bracket"})
                    next_sample = time.perf_counter()
            if engine_handle is not None and now >= next_sample:
                row["samples"].append(win.sample(engine_handle, engine_pid)); next_sample = time.perf_counter() + .01
            time.sleep(min(.01, max(0, next_sample - time.perf_counter())) if engine_handle is not None else .01)
    except Exception as error:
        row["errors"].append(repr(error)); stop_reason = "launch/observation error"
    finally:
        cleanup_start = time.perf_counter()
        if process is not None and process.poll() is None:
            # The existing fixed-runner policy permits only this live Popen PID
            # and its descendants; never select by image name or discover/kill
            # unrelated processes. The helper PID is not a sampled product.
            try:
                killed = subprocess.run([str(Path(os.environ["SystemRoot"]) / "System32/taskkill.exe"),
                    "/PID", str(process.pid), "/T", "/F"], capture_output=True,
                    timeout=10, creationflags=subprocess.CREATE_NO_WINDOW, check=False)
                row["owned_tree_cleanup"] = {"pid": process.pid, "exit_code": killed.returncode,
                    "stdout": killed.stdout[:4096].decode("utf-8", "replace"),
                    "stderr": killed.stderr[:4096].decode("utf-8", "replace")}
            except Exception as error:
                row["errors"].append("Owned PID-tree cleanup: " + repr(error))
        if process is not None:
            try:
                remaining = 10 - (time.perf_counter() - cleanup_start)
                row["exit_code"] = process.wait(timeout=remaining) if remaining > 0 else process.poll()
            except subprocess.TimeoutExpired:
                row["errors"].append("Owned process did not terminate within10s cleanup")
            row["observed_exit_perf_counter_ns" if row.get("exit_code") is not None
                else "cleanup_bound_perf_counter_ns"] = time.perf_counter_ns()
        for thread in readers:
            thread.join(max(0, 10 - (time.perf_counter() - cleanup_start)))
        if engine_handle is not None:
            row["engine_wait_state"] = int(win.kernel.WaitForSingleObject(engine_handle, 0))
            row["engine_terminal"] = row["engine_wait_state"] == 0
            if not row["engine_terminal"]:
                row["errors"].append("Admitted engine termination unresolved after owned launcher cleanup; no discovered PID kill attempted")
            if not win.kernel.CloseHandle(engine_handle):
                row["errors"].append("Close native engine sample handle: " + str(C.get_last_error()))
            row["engine_handle_closed_perf_counter_ns"] = time.perf_counter_ns()
        row["cleanup_seconds"] = time.perf_counter() - cleanup_start
        row["readers_terminal"] = all(not thread.is_alive() for thread in readers)
    row["stop_reason"] = stop_reason; row["logs"] = logs
    row["elapsed_seconds"] = time.perf_counter() - launch
    if engine_handle is None:
        row["memory_admission"]["state"] = "missing_or_unadmitted_marker"
        row["errors"].append("No native engine marker/handle admitted within the owned launch deadline/lifetime")
    good = [sample for sample in row["samples"] if sample["private_bytes"] is not None]
    if not good:
        row["errors"].append("No successful native engine PrivateUsage sample")
    gaps = [b["perf_counter_ns"] - a["perf_counter_ns"] for a, b in zip(row["samples"], row["samples"][1:])]
    row["memory_summary"] = {"target_interval_ns": 10000000, "sample_count": len(row["samples"]),
        "error_count": len(row["samples"]) - len(good), "gaps_ns": gaps,
        "maximum_gap_ns": max(gaps, default=None), "gaps_over20ms": sum(gap > 20000000 for gap in gaps),
        "engine_pid": engine_pid, "interval_begins_after_marker_admission": True,
        "qpc_frequency": win.qpc_frequency,
        "interval_first_perf_counter_ns": row["samples"][0]["perf_counter_ns"] if row["samples"] else None,
        "interval_last_perf_counter_ns": row["samples"][-1]["perf_counter_ns"] if row["samples"] else None,
        "sampled_engine_interval_first_bytes": good[0]["private_bytes"] if good else None,
        "sampled_engine_interval_min_bytes": min((s["private_bytes"] for s in good), default=None),
        "sampled_engine_interval_max_bytes": max((s["private_bytes"] for s in good), default=None),
        "scope": "PrivateUsage of marker-admitted native engine handle; sampled post-admission interval extrema, not full lifetime/unsampled peaks or widget-relative deltas"}
    report = folder / "run" / "rank-runtime.json"
    try:
        if report.is_file():
            value = read_json(report)
            row["product_report"] = pin(report)
            row["product_process_id"] = value.get("process_id")
            if (type(value.get("process_id")) is not int or value["process_id"] != engine_pid
                or not row["samples"] or any(sample["pid"] != value["process_id"] for sample in row["samples"])):
                row["errors"].append("Product raw process_id does not match every admitted native engine sample")
            if value.get("context") != context:
                row["errors"].append("Product raw context differs from fixed launch context")
            if type(value.get("qpc_frequency")) is not int or value["qpc_frequency"] != win.qpc_frequency:
                row["errors"].append("Product raw/native marker/external QPC frequency mismatch")
            if "process_marker" in row:
                if pin(marker) != row["process_marker"]["pin"]:
                    row["errors"].append("Native engine process marker changed after admission")
            row["product_completion"] = value.get("completion")
            row["missing_series"] = value.get("missing_series", [])
            row["trial_statuses"] = [{key: trial.get(key) for key in (
                "id", "endpoint", "required_initial_state", "status", "reason", "eligible")}
                for trial in value.get("trials", [])]
            row["widget_memory"] = join_widget_memory(row["samples"], row.get("process_marker", {}).get("value", {}),
                                                       value, engine_pid, win.qpc_frequency)
            row["errors"].extend("QPC memory join: " + error for error in row["widget_memory"]["errors"])
            if value.get("acceptance") is not False:
                row["errors"].append("Product report missing explicit acceptance:false")
        else:
            row["errors"].append("Product rank-runtime.json missing")
    except Exception as error:
        row["errors"].append("Retained product report: " + repr(error))
    write_json(folder / "supervisor.json", row)
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--receipt", required=True, type=Path)
    parser.add_argument("--build-receipt", required=True, type=Path)
    parser.add_argument("--build-sha256", required=True)
    args = parser.parse_args()
    output, receipt = args.output.resolve(), args.receipt.resolve()
    if os.name != "nt":
        parser.error("This fixed supervisor requires Windows")
    if not output.is_relative_to(ROOT) or not receipt.is_relative_to(ROOT):
        parser.error("Owned output and receipt must be inside the workspace")
    if output.exists() or receipt.exists():
        parser.error("Output stage and receipt must both be fresh")
    build_path = args.build_receipt.resolve()
    if not build_path.is_relative_to(ROOT) or not build_path.is_file():
        parser.error("Build receipt must be an existing file inside the workspace")
    if len(args.build_sha256) != 64 or any(char not in "0123456789abcdef" for char in args.build_sha256):
        parser.error("--build-sha256 requires an exact lowercase SHA256")
    output.mkdir(parents=False); receipt.parent.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter(); deadline = started + 600
    result = {"schema": 1, "producer_id": "p06f.rank.normal-loop.supervisor.001",
        "acceptance": False, "plan": PLAN, "state": "running", "runs": [],
        "bounds": {"external_cell_seconds": 45, "product_cell_seconds": 30,
            "context_seconds": 300, "parent_seconds": 600, "cleanup_seconds": 10,
            "owned_output_bytes": CAP, "one_live_product": True}, "errors": []}
    try:
        win = Win()
        result["clock_profile"] = win.clock_profile()
        result["memory_observer_contract"] = pin(ROOT / "docs/parity/reviews/G042-rank-runtime-observer-adoption-004.json")
        build = read_json(build_path)
        if build.get("step", {}).get("exit_code") != 0 or not build.get("products"):
            raise RuntimeError("Declared build receipt has not passed with pinned products; no product launched")
        result["build"] = pin(build_path)
        if result["build"]["sha256"] != args.build_sha256:
            raise RuntimeError("Declared build receipt differs from the integration owner's exact SHA256")
        checked_pins([build["inputs"]])
        inputs = read_json(build["inputs"]["path"])
        paired_path = ROOT / "build/p06f-001/paired-runtime-004.json"
        paired = read_json(paired_path); result["paired_launcher_source"] = pin(paired_path)
        prior = next(row["step"]["argv"] for row in paired["runs"] if row["context"] == "imgui_python")
        python, native = Path(prior[0]), Path(prior[8])
        products = build["products"]
        app = ROOT / next(row["path"] for row in products if Path(row["path"]).name == "imagejxx.exe")
        desktop = ROOT / next(row["path"] for row in products if Path(row["path"]).name.startswith("_desktop.") and row["path"].endswith(".pyd"))
        declared = inputs["inputs"] + products + paired["wrappers"] + [result["build"], build["inputs"], result["paired_launcher_source"],
            inputs["reused_core"], inputs["reused_native_extension"], result["memory_observer_contract"], pin(python), pin(Path(__file__))]
        if native.resolve() != (ROOT / inputs["reused_native_extension"]["path"]).resolve():
            raise RuntimeError("Paired launcher native path differs from declared build's reused native pin")
        result["pins_before"] = checked_pins(declared)
        plan_path = output / "plan.json"; write_json(plan_path, PLAN)
        for context in ("imgui_cpp", "imgui_python"):
            context_deadline = min(deadline, time.perf_counter() + 300)
            observation = {"context": context, "host": host(win),
                           "load_before": load_observation(win, context_deadline)}
            destination = output / context / "run"
            argv = [str(app), "--rank-runtime-case", str(plan_path), str(destination)] if context == "imgui_cpp" else [
                str(python), "-I", "-X", "utf8", "-B", "-c", WRAPPER,
                str(ROOT), str(native), str(desktop), str(plan_path), str(destination)]
            if time.perf_counter() >= context_deadline:
                raise RuntimeError("Context/parent deadline reached before launch: " + context)
            observation["run"] = run_one(win, argv, output / context, context_deadline, output, context)
            result["runs"].append(observation)
            if (observation["run"].get("exit_code") is None or not observation["run"].get("readers_terminal")
                or observation["run"].get("engine_terminal") is False):
                raise RuntimeError("Prior owned product/tree cleanup is unresolved; second launcher refused")
        result["state"] = "both_fixed_feasibility_launches_terminal_no_acceptance"
    except Exception as error:
        result["errors"].append(repr(error)); result["state"] = "failed_or_blocked_receipt_retained"
    finally:
        if "declared" in locals():
            try:
                result["pins_after"] = checked_pins(declared)
                result["pins_unchanged"] = result["pins_before"] == result["pins_after"]
            except Exception as error:
                result["errors"].append("Post-run pin assertion: " + repr(error)); result["state"] = "pin_mismatch"
        result["elapsed_seconds"] = time.perf_counter() - started
        result["owned_bytes_before_receipt"] = owned_bytes(output)
        if result["owned_bytes_before_receipt"] > CAP - 8 * 1024 * 1024:
            result["errors"].append("Owned artifact cap reserve reached")
        write_json(receipt, result)
    return int(bool(result["errors"]) or any(row["run"].get("exit_code") != 0 or row["run"]["errors"]
                                           for row in result["runs"]))


if __name__ == "__main__":
    raise SystemExit(main())
