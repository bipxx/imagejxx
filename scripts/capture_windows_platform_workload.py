"""Bounded external Windows observations; no platform/budget acceptance.

Prepare the contract's literal fixtures, or collect the independently observable
first subset of W01/W03/W04/W06 using immutable P06c3 packages. No headless input,
cross-thread Editor calls, clipboard changes, printing or user-process control.
"""
from __future__ import annotations

import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import time
import traceback
import zipfile
import zlib

CONTRACT_SHA = "e224a17817cc9088437e008d0157aa0bd952d7274b1ad083229c9756efca710d"
FIXTURE_SCHEMA = "imagejxx.windows-workload-fixtures.001"
REPORT_SCHEMA = "imagejxx.windows-os-subset.001"
MAX_OUTPUT = 256 * 1024 * 1024
MAX_JSON = 16 * 1024 * 1024
MAX_CAPTURE = 8 * 1024 * 1024
TAG = 0x49584F53  # Explicit SendInput marker; not a hardware-input claim.
ROOT = Path(__file__).resolve().parents[1]
PTR = C.c_size_t
LRESULT = C.c_ssize_t
WINFUNCTYPE = getattr(C, "WINFUNCTYPE", C.CFUNCTYPE)


def strict_json(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError(f"Duplicate JSON key: {key}")
            result[key] = value
        return result

    return json.loads(raw, object_pairs_hook=pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))


def digest(path):
    path = Path(path)
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return {"path": str(path.resolve()), "bytes": path.stat().st_size,
            "sha256": h.hexdigest()}


def same_pin(path, pin):
    actual = digest(path)
    if actual["bytes"] != pin["bytes"] or actual["sha256"] != pin["sha256"]:
        raise RuntimeError(f"Consumed identity changed: {path}")
    return actual


def contract_inputs(path, tool_sha):
    if os.name != "nt":
        raise RuntimeError("This observer requires Windows; no fallback")
    tool = digest(__file__)
    if tool["sha256"] != tool_sha.lower():
        raise RuntimeError("Observer source is not the reviewed tool hash")
    contract_pin = digest(path)
    if contract_pin["sha256"] != CONTRACT_SHA:
        raise RuntimeError("Expected the frozen workload proposal, not an edited contract")
    contract = strict_json(Path(path).read_bytes())
    if [row["id"] for row in contract["workload"]] != [f"W{i:02d}" for i in range(1, 7)]:
        raise RuntimeError("W01-W06 membership changed")
    if [row["context"] for row in contract["launches"]] != ["imgui_cpp", "imgui_python"]:
        raise RuntimeError("Launch membership changed")
    verified = []
    for pin in contract["consumed_files"] + contract["product_pins"]:
        resolved = (ROOT / pin["path"]).resolve()
        if ROOT not in resolved.parents:
            raise RuntimeError("Consumed path escapes workspace")
        verified.append(same_pin(resolved, pin))
    archive = ROOT / "build/p06c3-sdist001/imagejxx-0.2.0.tar.gz"
    with tarfile.open(archive, "r:gz") as source:
        for pin in contract["qualified_source_members"]:
            member = source.getmember(pin["member"])
            if not member.isfile() or member.size != pin["bytes"]:
                raise RuntimeError("Qualified source member changed")
            stream = source.extractfile(member)
            if stream is None or hashlib.sha256(stream.read()).hexdigest() != pin["sha256"]:
                raise RuntimeError("Qualified source-member hash mismatch")
    payload = installed_payload()
    return contract, {"tool": tool, "contract": contract_pin, "files": verified,
                      "installed_payload": payload}


def installed_payload():
    """Check every qualified wheel payload byte, not just the launch executable."""
    site = ROOT / "build/p06c3-env-ui001/Lib/site-packages"
    prefix = ROOT / "build/p06c3-install001"
    wheel = ROOT / "build/p06c3-dist-ui001/imagejxx-0.2.0-cp312-cp312-win_amd64.whl"
    prefix_names, package_names, rows = set(), set(), []
    with zipfile.ZipFile(wheel) as archive:
        for entry in archive.infolist():
            if entry.is_dir():
                continue
            name = entry.filename
            if name.endswith(".dist-info/RECORD"):
                continue  # pip regenerates this; prior distribution owns its audit.
            if entry.file_size > 64*1024*1024:
                raise RuntimeError("Qualified payload member size bound")
            raw = archive.read(entry)
            installed = site / name
            if installed.read_bytes() != raw:
                raise RuntimeError(f"Installed qualified payload changed: {name}")
            rows.append({"member": name, "bytes": len(raw),
                         "sha256": hashlib.sha256(raw).hexdigest()})
            if name.startswith("imagejxx/"):
                package_names.add(name)
            if name.startswith(("bin/", "include/", "lib/", "share/")):
                prefix_names.add(name)
                if (prefix / name).read_bytes() != raw:
                    raise RuntimeError(f"Qualified C++ prefix changed: {name}")
    actual_prefix = {str(path.relative_to(prefix)).replace("\\", "/")
                     for path in prefix.rglob("*") if path.is_file()}
    actual_package = {str(path.relative_to(site)).replace("\\", "/")
                      for path in (site / "imagejxx").rglob("*")
                      if path.is_file() and "__pycache__" not in path.parts}
    if actual_prefix != prefix_names or actual_package != package_names:
        raise RuntimeError("Qualified prefix/package payload set changed")
    return {"state": "byte_identical_to_pinned_wheel", "wheel_members": rows,
            "prefix_members": len(prefix_names), "package_members": len(package_names),
            "record_policy": "regenerated installed RECORD reused at prior qualified scope"}


class Output:
    def __init__(self, path, maximum=MAX_OUTPUT):
        self.root = Path(path).resolve()
        base = (ROOT / "build/windows-workload-001").resolve()
        if ROOT not in base.parents or base not in self.root.parents:
            raise RuntimeError("Fresh output must be below build/windows-workload-001")
        self.root.mkdir(parents=True, exist_ok=False)
        self.maximum = maximum
        self.written = 0
        self.streams = {}

    def path(self, member):
        path = (self.root / member).resolve()
        if self.root not in path.parents:
            raise RuntimeError("Output member escapes fresh attempt")
        return path

    def charge(self, count):
        if count < 0 or self.written + count > self.maximum:
            raise RuntimeError("Declared output-byte cap exceeded")
        self.written += count

    def write(self, member, raw, limit=MAX_JSON):
        if len(raw) > limit:
            raise RuntimeError(f"Single output member cap: {member}")
        self.charge(len(raw))
        path = self.path(member)
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("xb") as stream:
            stream.write(raw)
        return digest(path)

    def json(self, member, value):
        return self.write(member, (json.dumps(value, indent=2, ensure_ascii=True,
                                             allow_nan=False) + "\n").encode())

    def row(self, member, value):
        raw = (json.dumps(value, separators=(",", ":"), ensure_ascii=True,
                          allow_nan=False) + "\n").encode()
        if len(raw) > MAX_JSON:
            raise RuntimeError("JSONL row cap exceeded")
        self.charge(len(raw))
        if member not in self.streams:
            path = self.path(member)
            path.parent.mkdir(parents=True, exist_ok=True)
            self.streams[member] = path.open("xb", buffering=0)
        self.streams[member].write(raw)

    def close(self):
        for stream in self.streams.values():
            stream.close()
        self.streams.clear()

    def index(self):
        self.close()
        return [digest(path) for path in sorted(self.root.rglob("*")) if path.is_file()]


def prepare(contract, pins, out):
    """Core imports occur only in an explicitly requested preparation run."""
    started = time.monotonic()
    out.json("prepare.started.json", {"schema": FIXTURE_SCHEMA, "state": "running",
             "inputs": pins, "visible_launches": 0, "acceptance_claim": False})
    report = {"schema": FIXTURE_SCHEMA, "state": "failed", "inputs": pins,
              "fixtures": [], "visible_launches": 0, "acceptance_claim": False}
    try:
        import imagejxx as ij
        import imagejxx._native as native
        import numpy as np
        site = (ROOT / "build/p06c3-env-ui001/Lib/site-packages").resolve()
        if site not in Path(native.__file__).resolve().parents:
            raise RuntimeError("Wrong installed core origin")
        report["native_origin"] = digest(native.__file__)
        target = out.path("fixtures")
        target.mkdir()
        for item in contract["fixtures"]:
            if time.monotonic() - started > 110:
                raise TimeoutError("Preparation deadline before next fixture")
            if item["id"].startswith("F01"):
                z, y, x = np.ogrid[:2, :24, :32]
                pixels = (7*z + 3*y + x).astype(np.uint16)[..., None]
                cal = ij.SpatialCalibration()
            elif item["id"].startswith("F02"):
                t, c, z, y, x = np.ogrid[:2, :3, :4, :48, :64]
                pixels = (4096*t + 512*c + 64*z + 3*y + x).astype(np.uint16)[..., None]
                cal = ij.SpatialCalibration(**item["calibration"])
            else:
                z, y, x = np.ogrid[:8, :512, :512]
                pixels = ((17*z + 3*y + x) % 4096).astype(np.uint16)[..., None]
                cal = ij.SpatialCalibration()
            raw = pixels.astype("<u2", copy=False).tobytes(order="C")
            if (list(pixels.shape) != item["shape"] or len(raw) != item["raw_bytes"] or
                    hashlib.sha256(raw).hexdigest() != item["raw_sha256"]):
                raise RuntimeError("Literal fixture input mismatch")
            owner = ij.Document(ij.Dataset(pixels, axes=item["axes"], calibration=cal),
                                title=item["id"])
            view = owner.snapshot().display
            view.t, view.c = item["initial_tcz"][:2]
            view.crosshair = item["initial_crosshair_xyz"]
            if "channel_display" in item:
                view.properties = {"channel_display": item["channel_display"]}
            owner.set_display(view)
            path = target / item["filename"]
            if path.exists():
                raise RuntimeError("Fresh fixture path unexpectedly exists")
            ij.save_session(str(path), ij.SessionCapture([owner]))
            restored = ij.load_session(str(path)).documents[0].snapshot()
            if restored.dataset.numpy("TCZYXS").astype("<u2", copy=False).tobytes() != raw:
                raise RuntimeError("Fixture roundtrip changed literal pixels")
            encoded = digest(path)
            out.charge(encoded["bytes"])
            report["fixtures"].append({"id": item["id"], **encoded,
                                       "raw_sha256": item["raw_sha256"]})
        report["state"] = "prepared_fixtures_only"
    except BaseException as error:
        report["error"] = repr(error)
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        report["elapsed_seconds"] = time.monotonic() - started
        out.json("prepared.json", report)
        out.json("index.json", {"members": out.index(), "acceptance_claim": False})


class POINT(C.Structure):
    _fields_ = [("x", W.LONG), ("y", W.LONG)]


class RECT(C.Structure):
    _fields_ = [("left", W.LONG), ("top", W.LONG), ("right", W.LONG), ("bottom", W.LONG)]


class MOUSEINPUT(C.Structure):
    _fields_ = [("dx", W.LONG), ("dy", W.LONG), ("mouseData", W.DWORD),
                ("dwFlags", W.DWORD), ("time", W.DWORD), ("dwExtraInfo", PTR)]


class KEYBDINPUT(C.Structure):
    _fields_ = [("wVk", W.WORD), ("wScan", W.WORD), ("dwFlags", W.DWORD),
                ("time", W.DWORD), ("dwExtraInfo", PTR)]


class HARDWAREINPUT(C.Structure):
    _fields_ = [("uMsg", W.DWORD), ("wParamL", W.WORD), ("wParamH", W.WORD)]


class INPUTUNION(C.Union):
    _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT), ("hi", HARDWAREINPUT)]


class INPUT(C.Structure):
    _anonymous_ = ("value",)
    _fields_ = [("type", W.DWORD), ("value", INPUTUNION)]


class KBDLL(C.Structure):
    _fields_ = [("vkCode", W.DWORD), ("scanCode", W.DWORD), ("flags", W.DWORD),
                ("time", W.DWORD), ("dwExtraInfo", PTR)]


class GUIINFO(C.Structure):
    _fields_ = [("cbSize", W.DWORD), ("flags", W.DWORD), ("hwndActive", W.HWND),
                ("hwndFocus", W.HWND), ("hwndCapture", W.HWND), ("hwndMenuOwner", W.HWND),
                ("hwndMoveSize", W.HWND), ("hwndCaret", W.HWND), ("rcCaret", RECT)]


class MEMORY(C.Structure):
    _fields_ = [("cb", W.DWORD), ("PageFaultCount", W.DWORD)] + [
        (name, PTR) for name in ("PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                               "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
                               "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


class STARTUPINFO(C.Structure):
    _fields_ = [("cb", W.DWORD), ("lpReserved", W.LPWSTR), ("lpDesktop", W.LPWSTR),
                ("lpTitle", W.LPWSTR)] + [(name, W.DWORD) for name in
                ("dwX", "dwY", "dwXSize", "dwYSize", "dwXCountChars", "dwYCountChars",
                 "dwFillAttribute", "dwFlags")] + [("wShowWindow", W.WORD),
                ("cbReserved2", W.WORD), ("lpReserved2", C.c_void_p),
                ("hStdInput", W.HANDLE), ("hStdOutput", W.HANDLE), ("hStdError", W.HANDLE)]


class PROCESSINFO(C.Structure):
    _fields_ = [("hProcess", W.HANDLE), ("hThread", W.HANDLE),
                ("dwProcessId", W.DWORD), ("dwThreadId", W.DWORD)]


class JOBACTIVE(C.Structure):
    _fields_ = [("assigned", W.DWORD), ("count", W.DWORD), ("pids", PTR * 64)]


class JOBBASIC(C.Structure):
    _fields_ = [("process_time", C.c_int64), ("job_time", C.c_int64), ("flags", W.DWORD),
                ("min_ws", PTR), ("max_ws", PTR), ("active_limit", W.DWORD),
                ("affinity", PTR), ("priority", W.DWORD), ("scheduling", W.DWORD)]


class IOCOUNTERS(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in
                ("read_ops", "write_ops", "other_ops", "read_bytes", "write_bytes", "other_bytes")]


class JOBLIMIT(C.Structure):
    _fields_ = [("basic", JOBBASIC), ("io", IOCOUNTERS), ("process_memory", PTR),
                ("job_memory", PTR), ("peak_process_memory", PTR), ("peak_job_memory", PTR)]


class MONITORINFO(C.Structure):
    _fields_ = [("cbSize", W.DWORD), ("rcMonitor", RECT), ("rcWork", RECT),
                ("dwFlags", W.DWORD), ("szDevice", W.WCHAR * 32)]


class BITMAPHEADER(C.Structure):
    _fields_ = [("biSize", W.DWORD), ("biWidth", W.LONG), ("biHeight", W.LONG),
                ("biPlanes", W.WORD), ("biBitCount", W.WORD), ("biCompression", W.DWORD),
                ("biSizeImage", W.DWORD), ("biXPelsPerMeter", W.LONG),
                ("biYPelsPerMeter", W.LONG), ("biClrUsed", W.DWORD), ("biClrImportant", W.DWORD)]


def rect_values(rect):
    return [rect.left, rect.top, rect.right, rect.bottom]


def png(width, height, bgra):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        row = bgra[y*width*4:(y+1)*width*4]
        for x in range(width):
            rows.extend((row[4*x+2], row[4*x+1], row[4*x]))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


class Win32:
    def __init__(self, out):
        self.out = out
        self.user = C.WinDLL("user32", use_last_error=True, winmode=0x800)
        self.kernel = C.WinDLL("kernel32", use_last_error=True, winmode=0x800)
        self.psapi = C.WinDLL("psapi", use_last_error=True, winmode=0x800)
        self.gdi = C.WinDLL("gdi32", use_last_error=True, winmode=0x800)
        self.context = "observer"
        self.event_count = 0
        self.fatal = None
        self.acks = []
        self.hooks = []
        self.callbacks = []
        self.product = None
        self.neutral = None
        self.last_memory = 0.
        self.capture_count = 0
        self.sample = None
        self.enumerator = WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        self.hook_proc = WINFUNCTYPE(LRESULT, C.c_int, W.WPARAM, W.LPARAM)
        self.event_proc = WINFUNCTYPE(None, W.HANDLE, W.DWORD, W.HWND,
                                     W.LONG, W.LONG, W.DWORD, W.DWORD)
        self.window_proc = WINFUNCTYPE(LRESULT, W.HWND, W.UINT, W.WPARAM, W.LPARAM)
        self.monitor_proc = WINFUNCTYPE(W.BOOL, W.HANDLE, W.HDC, C.POINTER(RECT), W.LPARAM)
        self._prototypes()
        frequency = C.c_int64()
        if not self.kernel.QueryPerformanceFrequency(C.byref(frequency)):
            raise C.WinError(C.get_last_error())
        self.frequency = frequency.value

    def _prototypes(self):
        def proto(lib, name, result, args):
            fn = getattr(lib, name)
            fn.restype, fn.argtypes = result, args
        u, k, p, g = self.user, self.kernel, self.psapi, self.gdi
        for name in ("GetForegroundWindow",):
            proto(u, name, W.HWND, [])
        for name in ("IsWindow", "IsWindowVisible", "IsIconic"):
            proto(u, name, W.BOOL, [W.HWND])
        proto(u, "GetWindowThreadProcessId", W.DWORD, [W.HWND, C.POINTER(W.DWORD)])
        proto(u, "EnumWindows", W.BOOL, [self.enumerator, W.LPARAM])
        proto(u, "GetWindow", W.HWND, [W.HWND, W.UINT])
        proto(u, "GetWindowRect", W.BOOL, [W.HWND, C.POINTER(RECT)])
        proto(u, "GetClientRect", W.BOOL, [W.HWND, C.POINTER(RECT)])
        proto(u, "GetClassNameW", C.c_int, [W.HWND, W.LPWSTR, C.c_int])
        proto(u, "GetWindowTextW", C.c_int, [W.HWND, W.LPWSTR, C.c_int])
        proto(u, "GetGUIThreadInfo", W.BOOL, [W.DWORD, C.POINTER(GUIINFO)])
        proto(u, "GetDpiForWindow", W.UINT, [W.HWND])
        proto(u, "GetWindowDpiAwarenessContext", W.HANDLE, [W.HWND])
        proto(u, "GetThreadDpiAwarenessContext", W.HANDLE, [])
        proto(u, "SetThreadDpiAwarenessContext", W.HANDLE, [W.HANDLE])
        proto(u, "GetAwarenessFromDpiAwarenessContext", C.c_int, [W.HANDLE])
        proto(u, "SetForegroundWindow", W.BOOL, [W.HWND])
        proto(u, "SetWindowPos", W.BOOL, [W.HWND, W.HWND, C.c_int, C.c_int,
                                        C.c_int, C.c_int, W.UINT])
        proto(u, "PostMessageW", W.BOOL, [W.HWND, W.UINT, W.WPARAM, W.LPARAM])
        proto(u, "SendInput", W.UINT, [W.UINT, C.POINTER(INPUT), C.c_int])
        proto(u, "GetAsyncKeyState", C.c_short, [C.c_int])
        proto(u, "SetWindowsHookExW", W.HANDLE, [C.c_int, self.hook_proc, W.HINSTANCE, W.DWORD])
        proto(u, "UnhookWindowsHookEx", W.BOOL, [W.HANDLE])
        proto(u, "CallNextHookEx", LRESULT, [W.HANDLE, C.c_int, W.WPARAM, W.LPARAM])
        proto(u, "SetWinEventHook", W.HANDLE, [W.DWORD, W.DWORD, W.HMODULE,
                                              self.event_proc, W.DWORD, W.DWORD, W.DWORD])
        proto(u, "UnhookWinEvent", W.BOOL, [W.HANDLE])
        proto(u, "EnumDisplayMonitors", W.BOOL, [W.HDC, C.POINTER(RECT), self.monitor_proc, W.LPARAM])
        proto(u, "GetMonitorInfoW", W.BOOL, [W.HANDLE, C.POINTER(MONITORINFO)])
        proto(u, "GetDC", W.HDC, [W.HWND])
        proto(u, "ReleaseDC", C.c_int, [W.HWND, W.HDC])
        proto(u, "DefWindowProcW", LRESULT, [W.HWND, W.UINT, W.WPARAM, W.LPARAM])
        proto(u, "CreateWindowExW", W.HWND, [W.DWORD, W.LPCWSTR, W.LPCWSTR, W.DWORD,
            C.c_int, C.c_int, C.c_int, C.c_int, W.HWND, W.HMENU, W.HINSTANCE, C.c_void_p])
        proto(u, "ShowWindow", W.BOOL, [W.HWND, C.c_int])
        proto(u, "DestroyWindow", W.BOOL, [W.HWND])
        proto(u, "PeekMessageW", W.BOOL, [C.POINTER(W.MSG), W.HWND, W.UINT, W.UINT, W.UINT])
        proto(u, "TranslateMessage", W.BOOL, [C.POINTER(W.MSG)])
        proto(u, "DispatchMessageW", LRESULT, [C.POINTER(W.MSG)])
        proto(k, "GetModuleHandleW", W.HMODULE, [W.LPCWSTR])
        proto(k, "CloseHandle", W.BOOL, [W.HANDLE])
        proto(k, "GetCurrentProcessId", W.DWORD, [])
        proto(k, "QueryPerformanceCounter", W.BOOL, [C.POINTER(C.c_int64)])
        proto(k, "QueryPerformanceFrequency", W.BOOL, [C.POINTER(C.c_int64)])
        proto(k, "CreateJobObjectW", W.HANDLE, [C.c_void_p, W.LPCWSTR])
        proto(k, "SetInformationJobObject", W.BOOL, [W.HANDLE, C.c_int, C.c_void_p, W.DWORD])
        proto(k, "QueryInformationJobObject", W.BOOL, [W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.c_void_p])
        proto(k, "AssignProcessToJobObject", W.BOOL, [W.HANDLE, W.HANDLE])
        proto(k, "TerminateJobObject", W.BOOL, [W.HANDLE, W.UINT])
        proto(k, "TerminateProcess", W.BOOL, [W.HANDLE, W.UINT])
        proto(k, "ResumeThread", W.DWORD, [W.HANDLE])
        proto(k, "WaitForSingleObject", W.DWORD, [W.HANDLE, W.DWORD])
        proto(k, "GetExitCodeProcess", W.BOOL, [W.HANDLE, C.POINTER(W.DWORD)])
        proto(k, "GetProcessTimes", W.BOOL, [W.HANDLE] + [C.POINTER(W.FILETIME)] * 4)
        proto(k, "OpenProcess", W.HANDLE, [W.DWORD, W.BOOL, W.DWORD])
        proto(k, "CreateProcessW", W.BOOL, [W.LPCWSTR, W.LPWSTR, C.c_void_p,
            C.c_void_p, W.BOOL, W.DWORD, C.c_void_p, W.LPCWSTR,
            C.POINTER(STARTUPINFO), C.POINTER(PROCESSINFO)])
        proto(p, "GetProcessMemoryInfo", W.BOOL, [W.HANDLE, C.POINTER(MEMORY), W.DWORD])
        proto(p, "EnumProcessModulesEx", W.BOOL, [W.HANDLE, C.POINTER(W.HMODULE),
                                                  W.DWORD, C.POINTER(W.DWORD), W.DWORD])
        proto(p, "GetModuleFileNameExW", W.DWORD, [W.HANDLE, W.HMODULE, W.LPWSTR, W.DWORD])
        proto(g, "CreateCompatibleDC", W.HDC, [W.HDC])
        proto(g, "CreateCompatibleBitmap", W.HBITMAP, [W.HDC, C.c_int, C.c_int])
        proto(g, "SelectObject", W.HANDLE, [W.HDC, W.HANDLE])
        proto(g, "BitBlt", W.BOOL, [W.HDC, C.c_int, C.c_int, C.c_int, C.c_int,
                                    W.HDC, C.c_int, C.c_int, W.DWORD])
        proto(g, "GetDIBits", C.c_int, [W.HDC, W.HBITMAP, W.UINT, W.UINT,
                                        C.c_void_p, C.c_void_p, W.UINT])
        proto(g, "DeleteObject", W.BOOL, [W.HANDLE])
        proto(g, "DeleteDC", W.BOOL, [W.HDC])

    def stamp(self):
        counter = C.c_int64()
        if not self.kernel.QueryPerformanceCounter(C.byref(counter)):
            raise C.WinError(C.get_last_error())
        return {"qpc": counter.value, "qpc_frequency": self.frequency,
                "monotonic_seconds": time.monotonic()}

    def event(self, kind, **values):
        self.event_count += 1
        if self.event_count > 20000:
            raise RuntimeError("Context event cap exceeded")
        self.out.row(f"{self.context}/events.jsonl", {**self.stamp(), "kind": kind, **values})

    def pid(self, hwnd):
        pid = W.DWORD()
        tid = self.user.GetWindowThreadProcessId(hwnd, C.byref(pid))
        return pid.value, tid

    def snapshot(self, hwnd):
        pid, tid = self.pid(hwnd)
        rect, client = RECT(), RECT()
        self.user.GetWindowRect(hwnd, C.byref(rect))
        self.user.GetClientRect(hwnd, C.byref(client))
        gui = GUIINFO(); gui.cbSize = C.sizeof(gui)
        gui_ok = bool(self.user.GetGUIThreadInfo(tid, C.byref(gui)))
        name, title = C.create_unicode_buffer(256), C.create_unicode_buffer(512)
        self.user.GetClassNameW(hwnd, name, len(name))
        # Only this owned window's text is read, never a foreign window's title.
        self.user.GetWindowTextW(hwnd, title, len(title))
        awareness = self.user.GetWindowDpiAwarenessContext(hwnd)
        row = {**self.stamp(), "hwnd": int(hwnd), "pid": pid, "tid": tid,
               "class": name.value, "title": title.value,
               "window_rect": rect_values(rect), "client_rect": rect_values(client),
               "visible": bool(self.user.IsWindowVisible(hwnd)),
               "foreground_hwnd": int(self.user.GetForegroundWindow() or 0),
               "dpi": self.user.GetDpiForWindow(hwnd),
               "dpi_awareness": self.user.GetAwarenessFromDpiAwarenessContext(awareness),
               "gui_thread_observed": gui_ok}
        if gui_ok:
            row["gui_thread"] = {field: int(getattr(gui, field) or 0) for field in
                ("hwndActive", "hwndFocus", "hwndCapture", "hwndMenuOwner", "hwndMoveSize", "hwndCaret")}
            row["gui_thread"]["flags"] = gui.flags
        self.out.row(f"{self.context}/window-snapshots.jsonl", row)
        return row

    def check(self):
        if self.fatal:
            raise RuntimeError(self.fatal)
        size = sum(path.stat().st_size for path in self.out.root.rglob("*") if path.is_file())
        if size > self.out.maximum:
            raise RuntimeError("Output cap exceeded by external process")
        self.out.written = max(self.out.written, size)

    def pump(self):
        message = W.MSG()
        for _ in range(256):
            if not self.user.PeekMessageW(C.byref(message), None, 0, 0, 1):
                break
            self.user.TranslateMessage(C.byref(message))
            self.user.DispatchMessageW(C.byref(message))
        now = time.monotonic()
        if self.product and now - self.last_memory >= .1:
            self.last_memory = now
            self.product.sample_memory()
        self.check()

    def wait(self, predicate, seconds, deadline):
        until = min(time.monotonic() + seconds, deadline)
        while time.monotonic() < until:
            self.pump()
            result = predicate()
            if result:
                return result
            time.sleep(.025)
        self.pump()
        return predicate()

    def observe_hooks(self):
        def key_callback(code, message, pointer):
            try:
                if code >= 0:
                    value = C.cast(pointer, C.POINTER(KBDLL)).contents
                    if value.dwExtraInfo == TAG:
                        row = {"message": int(message), "vk": value.vkCode,
                               "scan": value.scanCode, "flags": value.flags,
                               "win32_time": value.time, "extra_info": value.dwExtraInfo}
                        self.acks.append(row)
                        self.event("os_keyboard_ack", **row)
            except BaseException as error:
                self.fatal = repr(error)
            return self.user.CallNextHookEx(None, code, message, pointer)

        def event_callback(hook, event, hwnd, obj, child, thread, event_time):
            try:
                pid, tid = self.pid(hwnd) if hwnd else (0, 0)
                owned = self.product is not None and pid in self.product.pids()
                if owned or hwnd == self.neutral or event == 3:
                    self.event("os_window_event", event=event, hwnd=int(hwnd or 0),
                               pid=pid, tid=tid, object_id=obj, child_id=child,
                               event_thread=thread, win32_time=event_time,
                               owned_product=owned, owned_neutral=hwnd == self.neutral)
            except BaseException as error:
                self.fatal = repr(error)

        key = self.hook_proc(key_callback)
        event = self.event_proc(event_callback)
        self.callbacks.extend([key, event])
        handle = self.user.SetWindowsHookExW(13, key, self.kernel.GetModuleHandleW(None), 0)
        if not handle:
            raise C.WinError(C.get_last_error())
        self.hooks.append(("keyboard", handle))
        for lo, hi in [(3, 3), (0x8002, 0x8005)]:
            handle = self.user.SetWinEventHook(lo, hi, None, event, 0, 0, 0)
            if not handle:
                raise C.WinError(C.get_last_error())
            self.hooks.append(("event", handle))

    def keys(self, hwnd, sequence, label, deadline):
        if int(self.user.GetForegroundWindow() or 0) != int(hwnd):
            self.event("input_not_sent", label=label, reason="foreground_not_owned_target")
            return False
        if any(self.user.GetAsyncKeyState(vk) & 0x8000 for vk in (0x10, 0x11, 0x12, 0x5b, 0x5c)):
            self.event("input_not_sent", label=label, reason="preexisting_modifier_pressed")
            return False
        inputs = (INPUT * len(sequence))()
        for i, (vk, up) in enumerate(sequence):
            inputs[i].type = 1
            inputs[i].ki = KEYBDINPUT(vk, 0, 2 if up else 0, 0, TAG)
        before = len(self.acks)
        C.set_last_error(0)
        sent = self.user.SendInput(len(inputs), inputs, C.sizeof(INPUT))
        self.event("send_input", label=label, target_hwnd=int(hwnd),
                   requested=[{"vk": vk, "up": up} for vk, up in sequence],
                   returned_count=sent, last_error=C.get_last_error(),
                   input_structure_bytes=C.sizeof(INPUT))
        acknowledged = self.wait(lambda: len(self.acks) - before >= len(sequence), 2, deadline)
        actual = self.acks[before:]
        # Alt combinations produce WM_SYSKEYDOWN/UP; distinguish up via flag.
        observed = [(r["vk"], bool(r["flags"] & 0x80)) for r in actual]
        exact = (observed == [(vk, up) for vk, up in sequence] and
                 all(row["flags"] & 0x10 for row in actual))
        self.event("send_input_acknowledgment", label=label,
                   acknowledged=bool(acknowledged), exact_order=exact,
                   acknowledgment_count=len(actual))
        if sent != len(sequence) or not acknowledged or not exact:
            # Only release keys this call requested down; never release user keys.
            released = (INPUT * len({vk for vk, up in sequence if not up}))()
            for row, vk in zip(released, sorted({vk for vk, up in sequence if not up})):
                row.type = 1; row.ki = KEYBDINPUT(vk, 0, 2, 0, TAG)
            self.user.SendInput(len(released), released, C.sizeof(INPUT))
            self.event("owned_injected_key_cleanup", label=label)
            return False
        return True

    def monitors(self):
        rows = []
        def callback(handle, dc, rect, data):
            try:
                info = MONITORINFO(); info.cbSize = C.sizeof(info)
                if not self.user.GetMonitorInfoW(handle, C.byref(info)):
                    raise C.WinError(C.get_last_error())
                rows.append({"handle": int(handle), "device": info.szDevice,
                             "monitor_rect": rect_values(info.rcMonitor),
                             "work_rect": rect_values(info.rcWork), "flags": info.dwFlags})
            except BaseException as error:
                self.fatal = repr(error)
            return len(rows) < 64
        fn = self.monitor_proc(callback)
        if not self.user.EnumDisplayMonitors(None, None, fn, 0):
            raise C.WinError(C.get_last_error())
        self.check()
        return rows

    def make_neutral(self):
        class WNDCLASS(C.Structure):
            _fields_ = [("style", W.UINT), ("proc", self.window_proc),
                ("class_extra", C.c_int), ("window_extra", C.c_int),
                ("instance", W.HINSTANCE), ("icon", W.HICON), ("cursor", W.HANDLE),
                ("background", W.HBRUSH), ("menu", W.LPCWSTR), ("name", W.LPCWSTR)]
        self.user.RegisterClassW.argtypes = [C.POINTER(WNDCLASS)]
        self.user.RegisterClassW.restype = W.WORD
        self.user.UnregisterClassW.argtypes = [W.LPCWSTR, W.HINSTANCE]
        self.user.UnregisterClassW.restype = W.BOOL
        callback = self.window_proc(lambda hwnd, msg, wp, lp:
                                    self.user.DefWindowProcW(hwnd, msg, wp, lp))
        self.callbacks.append(callback)
        self.neutral_class = f"ImageJxxOSObserver{os.getpid()}"
        self.instance = self.kernel.GetModuleHandleW(None)
        wc = WNDCLASS(0, callback, 0, 0, self.instance, None, None,
                      C.c_void_p(6), None, self.neutral_class)
        if not self.user.RegisterClassW(C.byref(wc)):
            raise C.WinError(C.get_last_error())
        hwnd = self.user.CreateWindowExW(0, self.neutral_class,
            "imagejxx owned focus observer", 0x00CF0000, 20, 20, 300, 120,
            None, None, self.instance, None)
        if not hwnd:
            raise C.WinError(C.get_last_error())
        self.neutral = int(hwnd)
        self.user.ShowWindow(hwnd, 4)  # Visible, no activation.
        self.event("neutral_created", hwnd=self.neutral, pid=os.getpid())

    def capture(self, hwnd, label):
        self.snapshot(hwnd)
        if self.capture_count >= 24:
            return {"state": "not_captured", "reason": "full_window_capture_count_cap"}
        if int(self.user.GetForegroundWindow() or 0) != int(hwnd):
            return {"state": "not_captured", "reason": "not_foreground"}
        rect = RECT()
        if not self.user.GetWindowRect(hwnd, C.byref(rect)):
            raise C.WinError(C.get_last_error())
        above = self.user.GetWindow(hwnd, 3)  # GW_HWNDPREV: higher in Z order.
        for _ in range(256):
            if not above:
                break
            other = RECT()
            if self.user.IsWindowVisible(above) and not self.user.IsIconic(above):
                self.user.GetWindowRect(above, C.byref(other))
                if (max(rect.left, other.left) < min(rect.right, other.right) and
                        max(rect.top, other.top) < min(rect.bottom, other.bottom)):
                    return {"state": "not_captured", "reason": "visible_window_above_target"}
            above = self.user.GetWindow(above, 3)
        else:
            return {"state": "not_captured", "reason": "bounded_occlusion_check_limit"}
        width, height = rect.right - rect.left, rect.bottom - rect.top
        if width <= 0 or height <= 0 or width*height*4 > MAX_CAPTURE:
            return {"state": "not_captured", "reason": "raw_capture_size_bound", "rect": rect_values(rect)}
        screen = self.user.GetDC(None)
        memory = self.gdi.CreateCompatibleDC(screen)
        bitmap = self.gdi.CreateCompatibleBitmap(screen, width, height)
        if not screen or not memory or not bitmap:
            if bitmap: self.gdi.DeleteObject(bitmap)
            if memory: self.gdi.DeleteDC(memory)
            if screen: self.user.ReleaseDC(None, screen)
            raise C.WinError(C.get_last_error())
        previous = self.gdi.SelectObject(memory, bitmap)
        try:
            if not previous or previous == C.c_void_p(-1).value:
                raise C.WinError(C.get_last_error())
            if not self.gdi.BitBlt(memory, 0, 0, width, height, screen,
                                   rect.left, rect.top, 0x00CC0020 | 0x40000000):
                raise C.WinError(C.get_last_error())
            self.gdi.SelectObject(memory, previous)
            previous = None
            header = BITMAPHEADER(C.sizeof(BITMAPHEADER), width, -height, 1, 32,
                                  0, width*height*4, 0, 0, 0, 0)
            raw = C.create_string_buffer(width*height*4)
            if self.gdi.GetDIBits(memory, bitmap, 0, height, raw, C.byref(header), 0) != height:
                raise C.WinError(C.get_last_error())
            self.capture_count += 1
            member = f"{self.context}/captures/{self.capture_count:02d}-{label}.png"
            record = self.out.write(member, png(width, height, raw.raw), MAX_CAPTURE)
            observation = {**self.stamp(), "state": "observed", "hwnd": int(hwnd),
                "rect": rect_values(rect), "method": "foreground-owned-screen-BitBlt/GetDIBits",
                "image": record, "gl_context_claim": False}
            self.out.row(f"{self.context}/captures.jsonl", observation)
            return observation
        finally:
            if previous: self.gdi.SelectObject(memory, previous)
            self.gdi.DeleteObject(bitmap)
            self.gdi.DeleteDC(memory)
            self.user.ReleaseDC(None, screen)

    def close(self):
        for kind, handle in reversed(self.hooks):
            if kind == "keyboard": self.user.UnhookWindowsHookEx(handle)
            else: self.user.UnhookWinEvent(handle)
        self.hooks.clear()
        if self.neutral:
            self.user.DestroyWindow(self.neutral)
            self.neutral = None
            self.user.UnregisterClassW(self.neutral_class, self.instance)
        self.callbacks.clear()


class JobProcess:
    """Suspended creation + job containment, including a Python launcher child."""
    def __init__(self, api, argv, cwd, env, stdout, stderr):
        import msvcrt
        self.api = api
        self.process = None
        self.job = None
        self.handles = {}
        self.closed = False
        self.argv = argv
        self.stdout, self.stderr = stdout, stderr
        k = api.kernel
        job = k.CreateJobObjectW(None, None)
        if not job:
            raise C.WinError(C.get_last_error())
        self.job = job
        limits = JOBLIMIT()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
        if not k.SetInformationJobObject(job, 9, C.byref(limits), C.sizeof(limits)):
            k.CloseHandle(job); self.job = None
            raise C.WinError(C.get_last_error())
        info = STARTUPINFO(); info.cb = C.sizeof(info)
        info.dwFlags = 0x100  # STARTF_USESTDHANDLES, not hidden UI.
        self.null = open(os.devnull, "rb")
        inherited = [msvcrt.get_osfhandle(stream.fileno()) for stream in
                     (self.null, stdout, stderr)]
        info.hStdInput, info.hStdOutput, info.hStdError = inherited
        command = C.create_unicode_buffer(subprocess.list2cmdline(argv))
        environment = C.create_unicode_buffer("\0".join(
            f"{key}={value}" for key, value in sorted(env.items(), key=lambda row: row[0].upper())) + "\0\0")
        pi = PROCESSINFO()
        error_code = 0
        spawn_error = False
        try:
            for handle in inherited:
                os.set_handle_inheritable(handle, True)
            ok = k.CreateProcessW(argv[0], command, None, None, True,
                                  0x4 | 0x400 | 0x08000000, environment, str(cwd), C.byref(info), C.byref(pi))
            error_code = C.get_last_error()
        except BaseException:
            k.CloseHandle(job); self.job = None; spawn_error = True
            raise
        finally:
            for handle in inherited:
                try:
                    os.set_handle_inheritable(handle, False)
                except OSError:
                    if not spawn_error:
                        raise
            if spawn_error:
                self.null.close()
        if not ok:
            k.CloseHandle(job); self.job = None; self.null.close()
            raise C.WinError(error_code)
        self.process = pi.hProcess
        self.pid = int(pi.dwProcessId)
        self.handles[self.pid] = pi.hProcess
        try:
            self.creation = self.creation_time(pi.hProcess)
            if not k.AssignProcessToJobObject(job, pi.hProcess):
                raise C.WinError(C.get_last_error())
            if k.ResumeThread(pi.hThread) == 0xffffffff:
                raise C.WinError(C.get_last_error())
        except BaseException:
            k.TerminateProcess(pi.hProcess, 1)
            k.WaitForSingleObject(pi.hProcess, 5000)
            self.close()
            raise
        finally:
            k.CloseHandle(pi.hThread)

    def creation_time(self, handle):
        values = [W.FILETIME() for _ in range(4)]
        if not self.api.kernel.GetProcessTimes(handle, *(C.byref(v) for v in values)):
            raise C.WinError(C.get_last_error())
        return (values[0].dwHighDateTime << 32) | values[0].dwLowDateTime

    def pids(self):
        if not self.job:
            return set()
        active = JOBACTIVE()
        if not self.api.kernel.QueryInformationJobObject(
                self.job, 3, C.byref(active), C.sizeof(active), None):
            raise C.WinError(C.get_last_error())
        if active.assigned > 64 or active.count > 64:
            raise RuntimeError("Owned job process-count bound exceeded")
        return {int(active.pids[i]) for i in range(active.count)}

    def handle(self, pid):
        if pid not in self.pids():
            raise RuntimeError("Requested PID is not in this owned job")
        if pid not in self.handles:
            handle = self.api.kernel.OpenProcess(0x1000 | 0x400 | 0x10 | 0x100000, False, pid)
            if not handle:
                raise C.WinError(C.get_last_error())
            self.handles[pid] = handle
        return self.handles[pid]

    def window(self):
        owned = self.pids()
        found = []
        def callback(hwnd, extra):
            pid, tid = self.api.pid(hwnd)
            if pid in owned and self.api.user.IsWindowVisible(hwnd):
                name = C.create_unicode_buffer(256)
                self.api.user.GetClassNameW(hwnd, name, len(name))
                if name.value.startswith("GLFW"):
                    found.append(int(hwnd))
            return True
        fn = self.api.enumerator(callback)
        if not self.api.user.EnumWindows(fn, 0):
            raise C.WinError(C.get_last_error())
        if len(found) > 1:
            raise RuntimeError("More than one visible product GLFW window")
        return found[0] if found else None

    def sample_memory(self):
        for pid in sorted(self.pids()):
            handle = self.handle(pid)
            counters = MEMORY(); counters.cb = C.sizeof(counters)
            if not self.api.psapi.GetProcessMemoryInfo(handle, C.byref(counters), C.sizeof(counters)):
                self.api.event("memory_unavailable", pid=pid, error=C.get_last_error())
                continue
            self.api.out.row(f"{self.api.context}/memory.jsonl", {
                **self.api.stamp(), "pid": pid, "creation_filetime": self.creation_time(handle),
                "working_set_bytes": int(counters.WorkingSetSize),
                "peak_working_set_bytes": int(counters.PeakWorkingSetSize),
                "private_usage_bytes": int(counters.PrivateUsage),
                "pagefile_usage_bytes": int(counters.PagefileUsage),
                "scope": "this_owned_pid; no GPU/whole-process-tree inference"})

    def modules(self, hwnd, deadline):
        pid, _ = self.api.pid(hwnd)
        handle = self.handle(pid)
        array = (W.HMODULE * 1024)(); needed = W.DWORD()
        if not self.api.psapi.EnumProcessModulesEx(handle, array, C.sizeof(array), C.byref(needed), 3):
            raise C.WinError(C.get_last_error())
        if needed.value > C.sizeof(array):
            raise RuntimeError("Module-count bound exceeded")
        rows = []
        for module in array[:needed.value // C.sizeof(W.HMODULE)]:
            if time.monotonic() >= deadline:
                rows.append({"state": "remaining_modules_not_observed_deadline"})
                break
            path = C.create_unicode_buffer(32768)
            if not self.api.psapi.GetModuleFileNameExW(handle, module, path, len(path)):
                rows.append({"module": int(module or 0), "error": C.get_last_error()})
                continue
            row = {"path": path.value}
            try:
                if Path(path.value).stat().st_size > 128*1024*1024:
                    row["hash_state"] = "unavailable_file_over_128MiB_bound"
                else:
                    row.update(digest(path.value))
                row["file_version"] = file_version(path.value)
            except OSError as error:
                row["read_error"] = repr(error)
            rows.append(row)
            self.api.pump()
        return {**self.api.stamp(), "pid": pid, "creation_filetime": self.creation_time(handle),
                "modules": rows, "compiler_invocation_claim": False,
                "gl_context_or_active_adapter_claim": False}

    def exit_code(self):
        code = W.DWORD()
        if not self.api.kernel.GetExitCodeProcess(self.process, C.byref(code)):
            raise C.WinError(C.get_last_error())
        return code.value

    def close(self):
        if self.closed:
            return
        self.closed = True
        # Closing a live assigned job intentionally terminates only its members.
        if self.job:
            self.api.kernel.CloseHandle(self.job)
            self.job = None
        for handle in self.handles.values():
            self.api.kernel.CloseHandle(handle)
        self.handles.clear()
        self.process = None
        self.null.close()


def file_version(path):
    lib = C.WinDLL("version", use_last_error=True, winmode=0x800)
    lib.GetFileVersionInfoSizeW.argtypes = [W.LPCWSTR, C.POINTER(W.DWORD)]
    lib.GetFileVersionInfoSizeW.restype = W.DWORD
    dummy = W.DWORD()
    size = lib.GetFileVersionInfoSizeW(path, C.byref(dummy))
    if not size:
        return {"state": "no_version_resource", "error": C.get_last_error()}
    if size > 1024*1024:
        return {"state": "resource_over_1MiB_bound"}
    lib.GetFileVersionInfoW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p]
    lib.GetFileVersionInfoW.restype = W.BOOL
    lib.VerQueryValueW.argtypes = [C.c_void_p, W.LPCWSTR, C.POINTER(C.c_void_p), C.POINTER(W.UINT)]
    lib.VerQueryValueW.restype = W.BOOL
    data = C.create_string_buffer(size)
    if not lib.GetFileVersionInfoW(path, 0, size, data):
        return {"state": "read_failed", "error": C.get_last_error()}
    pointer, count = C.c_void_p(), W.UINT()
    if not lib.VerQueryValueW(data, "\\", C.byref(pointer), C.byref(count)) or count.value < 52:
        return {"state": "fixed_version_unavailable"}
    fields = C.cast(pointer, C.POINTER(W.DWORD * 13)).contents
    if fields[0] != 0xFEEF04BD:
        return {"state": "invalid_fixed_version_signature"}
    return {"state": "observed", "version": [fields[2] >> 16, fields[2] & 65535,
                                               fields[3] >> 16, fields[3] & 65535]}


def supervised_prepare(contract, pins, out, args):
    api = Win32(out)
    process = None
    streams = []
    report = {"schema": FIXTURE_SCHEMA, "state": "failed", "inputs": pins,
              "visible_launches": 0, "acceptance_claim": False}
    argv = [sys.executable, "-I", "-X", "utf8", "-B", str(Path(__file__).resolve()),
            "--_prepare-worker", "--contract", str(Path(args.contract).resolve()),
            "--tool-sha256", args.tool_sha256, "--output", str(out.path("materialized"))]
    out.json("prepare.started.json", {**report, "state": "running", "argv": argv,
                                      "timeout_seconds": 120})
    started = time.monotonic()
    try:
        for member in ("prepare.stdout", "prepare.stderr"):
            streams.append(out.path(member).open("xb", buffering=0))
        process = JobProcess(api, argv, ROOT, dict(os.environ), *streams)
        deadline = started + 110
        terminal = api.wait(lambda: not process.pids(), 110, deadline)
        if not terminal:
            raise TimeoutError("Fixture worker exceeded its active deadline")
        if process.exit_code() != 0:
            raise RuntimeError("Fixture worker failed; inspect original prepare.stderr")
        prepared = strict_json(out.path("materialized/prepared.json").read_bytes())
        if prepared["state"] != "prepared_fixtures_only":
            raise RuntimeError("Worker has no complete preparation")
        report = prepared
        report["supervisor"] = {"argv": argv, "job_contained": True,
                                 "child_receipt": digest(out.path("materialized/prepared.json"))}
    except BaseException as error:
        report["error"] = repr(error)
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        if process:
            if process.pids():
                api.kernel.TerminateJobObject(process.job, 1)
                api.wait(lambda: not process.pids(), 5, started + 120)
            process.close()
        for stream in streams:
            stream.close()
        report["elapsed_seconds"] = time.monotonic() - started
        out.json("prepared.json", report)
        out.json("index.json", {"members": out.index(), "acceptance_claim": False})


def verify_prepared(path, contract):
    record = strict_json(Path(path).read_bytes())
    if (record.get("schema") == FIXTURE_SCHEMA and
            record.get("state") == "prepared_fixtures_only"):
        bound_contract = record["inputs"]["contract"]
    elif (record.get("id") in {"windows-workload.prepare.001", "windows-workload.prepare.002"} and
            record.get("state") == "prepared_fixtures_only_no_platform_execution" and
            record.get("visible_launches") == 0):
        # Explicit receipt adapter for root's source-bound snapshot() repair.
        # It consumes data only and never executes the failed embedded code.
        bound_contract = record["contract"]
    else:
        raise RuntimeError("Expected an exact recognized complete preparation receipt")
    if bound_contract["sha256"] != CONTRACT_SHA:
        raise RuntimeError("Preparation consumes a different input contract")
    if [row["id"] for row in record["fixtures"]] != [row["id"] for row in contract["fixtures"]]:
        raise RuntimeError("Fixture membership changed")
    files = {}
    for actual, expected in zip(record["fixtures"], contract["fixtures"]):
        if actual["raw_sha256"] != expected["raw_sha256"]:
            raise RuntimeError("Literal fixture hash changed")
        resolved = Path(actual["path"]).resolve()
        if (ROOT / "build/windows-workload-001").resolve() not in resolved.parents:
            raise RuntimeError("Prepared file is outside the owned stage")
        same_pin(resolved, actual)
        files[expected["filename"]] = str(resolved)
    return record, files


def uia_inventory(api, process, hwnd, label, deadline):
    """External bounded UIA query, never UIA invocation or fabricated names."""
    pid, _ = api.pid(hwnd)
    process.handle(pid)  # Must still be a member of the owned job.
    shell = Path("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe")
    source = r'''
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=New-Object System.Text.UTF8Encoding($false)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$target=[System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]HWND_VALUE)
if ($target.Current.ProcessId -ne PID_VALUE) { throw 'UIA root owner changed' }
$walker=[System.Windows.Automation.TreeWalker]::RawViewWalker
$queue=New-Object 'System.Collections.Generic.Queue[object]'
$queue.Enqueue(@{element=$target;depth=0;parent=$null})
$rows=New-Object 'System.Collections.Generic.List[object]'
$seen=New-Object 'System.Collections.Generic.HashSet[string]'
$truncated=$false
while ($queue.Count -gt 0 -and $rows.Count -lt 2048) {
  $item=$queue.Dequeue();$e=$item.element
  try {
    $runtime=($e.GetRuntimeId() -join ',')
    if (!$seen.Add($runtime)) { continue }
    $v=$e.Current
    if ($v.ProcessId -ne PID_VALUE) { continue }
    $r=$v.BoundingRectangle
    $value=$null;$pattern=$null
    if ($e.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern,[ref]$pattern)) { $value=$pattern.Current.Value }
    $index=$rows.Count
    $rows.Add(@{index=$index;parent=$item.parent;depth=$item.depth;runtime_id=$runtime;process_id=$v.ProcessId;name=$v.Name;control_type=$v.ControlType.ProgrammaticName;automation_id=$v.AutomationId;class=$v.ClassName;hwnd=$v.NativeWindowHandle;enabled=$v.IsEnabled;offscreen=$v.IsOffscreen;has_keyboard_focus=$v.HasKeyboardFocus;focusable=$v.IsKeyboardFocusable;rect=@($r.X,$r.Y,$r.Width,$r.Height);value=$value;patterns=@($e.GetSupportedPatterns() | ForEach-Object { $_.ProgrammaticName })})
    if ($item.depth -ge 16) { $truncated=$true;continue }
    $child=$walker.GetFirstChild($e)
    $siblings=0
    while ($null -ne $child -and $siblings -lt 2048 -and $queue.Count -lt 2048) {
      $queue.Enqueue(@{element=$child;depth=$item.depth+1;parent=$index})
      $child=$walker.GetNextSibling($child);$siblings++
    }
    if ($null -ne $child) { $truncated=$true }
  } catch { $rows.Add(@{depth=$item.depth;error=$_.Exception.Message}) }
}
if ($queue.Count -gt 0) { $truncated=$true }
@{state='observed_raw_semantic_inventory';pid=PID_VALUE;hwnd=HWND_VALUE;nodes=$rows.ToArray();truncated=$truncated;accessibility_acceptance=$false;narrator_observed=$false} | ConvertTo-Json -Depth 12 -Compress
'''.replace("HWND_VALUE", str(int(hwnd))).replace("PID_VALUE", str(pid))
    argv = [str(shell), "-NoLogo", "-NoProfile", "-NonInteractive", "-Command", source]
    directory = api.out.path(f"{api.context}/uia/{label}")
    directory.mkdir(parents=True)
    api.out.json(f"{api.context}/uia/{label}/command.json",
                 {"argv": argv, "tool": digest(shell), "timeout_seconds": 10,
                  "target_pid": pid, "target_hwnd": int(hwnd)})
    worker = None
    stdout, stderr = (directory / "stdout.json").open("xb", buffering=0), (directory / "stderr.log").open("xb", buffering=0)
    try:
        worker = JobProcess(api, argv, directory, dict(os.environ), stdout, stderr)
        until = min(time.monotonic() + 10, deadline)
        if not api.wait(lambda: not worker.pids(), 10, until):
            return {"state": "observation_limited", "reason": "UIA_worker_timeout"}
        if worker.exit_code() != 0:
            return {"state": "observation_limited", "reason": "UIA_worker_failed",
                    "exit_code": worker.exit_code()}
        if (directory / "stdout.json").stat().st_size > MAX_JSON:
            raise RuntimeError("UIA output cap")
        raw = strict_json((directory / "stdout.json").read_bytes())
        if raw.get("pid") != pid or raw.get("hwnd") != int(hwnd) or len(raw.get("nodes", [])) > 2048:
            raise RuntimeError("UIA observation identity/membership mismatch")
        return {"state": "observed", "raw": digest(directory / "stdout.json"),
                "node_count": len(raw["nodes"]), "truncated": raw["truncated"],
                "accessibility_acceptance": False, "narrator_observed": False}
    finally:
        if worker:
            if worker.pids():
                api.kernel.TerminateJobObject(worker.job, 1)
                api.wait(lambda: not worker.pids(), 2, min(deadline + 2, time.monotonic() + 2))
            worker.close()
        stdout.close(); stderr.close()


def close_product(api, process, hwnd, deadline):
    start = time.monotonic()
    result = {"requested": True, "method": "no_window", "forced": False}
    try:
        if process.pids() and hwnd and api.user.IsWindow(hwnd):
            if int(api.user.GetForegroundWindow() or 0) == int(hwnd):
                ok = api.keys(hwnd, [(0x12, False), (0x73, False), (0x73, True), (0x12, True)],
                              "normal_close.AltF4", min(deadline, start + 3))
                result["method"] = "SendInput_AltF4" if ok else "AltF4_acknowledgment_incomplete"
            if process.pids():
                # An explicit owned HWND close request is normal-close evidence,
                # not a keyboard-input claim. Never send keys to a foreign HWND.
                if result["method"] != "SendInput_AltF4":
                    api.user.PostMessageW(hwnd, 0x10, 0, 0)
                    result["fallback"] = "PostMessage_owned_WM_CLOSE"
            normal = api.wait(lambda: not process.pids(), 6, min(deadline, start + 8))
        else:
            normal = not process.pids()
        result["normal_terminal"] = bool(normal)
        if not normal:
            result["forced"] = True
            api.kernel.TerminateJobObject(process.job, 1)
            result["forced_terminal"] = bool(api.wait(lambda: not process.pids(), 2,
                                                       min(deadline, start + 10)))
        result["launcher_exit_code"] = process.exit_code()
        result["remaining_owned_pids"] = sorted(process.pids())
        return result
    except BaseException as error:
        result["cleanup_observer_error"] = repr(error)
        result["forced"] = True
        api.kernel.TerminateJobObject(process.job, 1)
        # Observer/cap failures must not bypass owned process cleanup. This
        # final bounded poll intentionally does not dispatch failed hooks.
        until = min(deadline, start + 10)
        while process.pids() and time.monotonic() < until:
            time.sleep(.025)
        result["remaining_owned_pids"] = sorted(process.pids())
        raise
    finally:
        result["elapsed_seconds"] = time.monotonic() - start
        try:
            api.event("owned_process_cleanup", **result)
        finally:
            api.product = None
            process.close()


def pending_ledger():
    return {
        "W01": {"state": "partial_observation_pending", "launches": [], "focus_roundtrips": [],
                 "native_modal": "pending_not_executed", "actual_gl_context": "observation_limited_G01"},
        "W02": {"state": "pending_not_executed", "reason": "interactive_widget/state work outside first subset"},
        "W03": {"state": "partial_observation_pending", "positions": [],
                 "font_hit_target_and_mixed_dpi_correctness": "pending_not_inferred"},
        "W04": {"state": "partial_observation_pending", "main_window_inventory": [],
                 "menu_text_canvas_modal_states": "pending_not_executed",
                 "narrator_announcements": "pending_not_executed", "accessibility_acceptance": False},
        "W05": {"state": "pending_not_executed", "reason": "no clipboard/dialog/printing changes in first subset"},
        "W06": {"state": "partial_observation_pending", "modules": [],
                 "process_memory": "pending", "navigation_preview_apply_latency": "pending_not_executed",
                 "gpu_attribution": "observation_limited_not_measured", "historic_compiler_invocation": "not_newly_observed",
                 "benchmark_or_budget_acceptance": False},
    }


def collect(contract, pins, out, args):
    prepared, files = verify_prepared(args.prepared, contract)
    report = {"schema": REPORT_SCHEMA, "state": "running", "inputs": pins,
              "prepared": digest(args.prepared), "acceptance_claim": False,
              "platform_accepted": False, "budgets_accepted": False,
              "first_subset_only": True, "parent_seconds": 300,
              "per_context_seconds_including_cleanup": 120,
              "contexts": [], "cleanup": []}
    out.json("run.started.json", report)
    started = time.monotonic()
    parent_deadline = started + 290  # Final cleanup/report has the last10seconds.
    api = Win32(out)
    old_awareness = None
    try:
        old_awareness = api.user.SetThreadDpiAwarenessContext(C.c_void_p(-4))
        awareness = api.user.GetThreadDpiAwarenessContext()
        report["observer_dpi_awareness"] = {
            "set_thread_per_monitor_v2_returned": bool(old_awareness),
            "active": api.user.GetAwarenessFromDpiAwarenessContext(awareness),
            "scope": "observer thread only; target/global scaling unchanged"}
        report["original_foreground_hwnd"] = int(api.user.GetForegroundWindow() or 0)
        api.make_neutral()
        api.observe_hooks()
        selected = [row for row in contract["launches"]
                    if args.context == "both" or row["context"] == args.context]
        for launch in selected:
            api.context = launch["context"]
            api.event_count = 0
            api.capture_count = 0
            context_start = time.monotonic()
            context_terminal = min(context_start + 120, parent_deadline)
            active_deadline = context_terminal - 10
            context = {"context": api.context, "state": "running",
                       "workload": pending_ledger(), "launch_attempts": []}
            report["contexts"].append(context)
            monitors = api.monitors()
            out.json(f"{api.context}/monitor-inventory.json", {
                **api.stamp(), "monitors": monitors,
                "physical_topology": "actual_EnumDisplayMonitors; no adapter-inventory inference"})
            for ordinal in (1, 2):
                if time.monotonic() + 20 >= active_deadline:
                    raise TimeoutError("Insufficient bounded time for the next declared relaunch")
                # Every launch rechecks full source/archive and installed bytes.
                contract_inputs(args.contract, args.tool_sha256)
                verify_prepared(args.prepared, contract)
                directory = out.path(f"{api.context}/launch{ordinal:02d}")
                directory.mkdir(parents=True)
                preferences = out.path(f"{api.context}/preferences")
                preferences.mkdir(exist_ok=True)
                argv = []
                for item in launch["argv"]:
                    value = item.replace("<ROOT>", str(ROOT))
                    if value.startswith("<PREP>/fixtures/"):
                        value = files[Path(value).name]
                    argv.append(value)
                env = dict(os.environ)
                env["IMAGEJXX_CONFIG_DIR"] = str(preferences)
                env["PYTHONDONTWRITEBYTECODE"] = "1"
                command = {"argv": argv, "cwd": str(directory),
                           "environment_overrides": {key: env[key] for key in
                                ("IMAGEJXX_CONFIG_DIR", "PYTHONDONTWRITEBYTECODE")},
                           "ordinal": ordinal, "visible": True,
                           "remaining_context_seconds": context_terminal - time.monotonic(),
                           "job_creation": "suspended; assign kill-on-close job; resume"}
                out.json(f"{api.context}/launch{ordinal:02d}/command.json", command)
                streams = [(directory / name).open("xb", buffering=0)
                           for name in ("stdout.log", "stderr.log")]
                process = None
                hwnd = None
                attempt = {"ordinal": ordinal, "state": "running"}
                context["launch_attempts"].append(attempt)
                try:
                    before = api.stamp()
                    process = JobProcess(api, argv, directory, env, *streams)
                    api.product = process
                    after_create = api.stamp()
                    attempt.update(launcher_pid=process.pid, launcher_creation_filetime=process.creation,
                                   before_creation=before, after_creation=after_create)
                    api.event("product_created", **attempt)
                    hwnd = api.wait(process.window, 15, active_deadline)
                    if not hwnd:
                        raise RuntimeError("No owned visible GLFW window within startup bound")
                    # No hidden render/API hooks: current native state stays opaque.
                    attempt["visible_window"] = api.snapshot(hwnd)
                    attempt["visible_hwnd_after_create_ms"] = (
                        attempt["visible_window"]["qpc"] - after_create["qpc"]) * 1000 / api.frequency
                    attempt["latency_scope"] = "first_visible_HWND; not stable-render/GL/widget readiness"
                    attempt["owned_job_pids"] = sorted(process.pids())
                    if int(api.user.GetForegroundWindow() or 0) != int(hwnd):
                        requested = bool(api.user.SetForegroundWindow(hwnd))
                        api.event("foreground_setup_request", hwnd=hwnd, returned=requested)
                        api.wait(lambda: int(api.user.GetForegroundWindow() or 0) == hwnd, 1, active_deadline)
                    attempt["capture"] = api.capture(hwnd, f"launch{ordinal:02d}-initial")
                    modules = process.modules(hwnd, min(active_deadline, time.monotonic() + 10))
                    module_pin = out.json(f"{api.context}/launch{ordinal:02d}/modules.json", modules)
                    context["workload"]["W06"]["modules"].append(module_pin)
                    context["workload"]["W06"]["process_memory"] = "observed_per_owned_pid_during_first_subset"
                    if ordinal == 1:
                        focus = context["workload"]["W01"]["focus_roundtrips"]
                        # Prime only owned windows' order. These setup calls do
                        # not receive input/focus-transfer proof credit.
                        api.user.SetForegroundWindow(api.neutral)
                        api.wait(lambda: int(api.user.GetForegroundWindow() or 0) == api.neutral, 1, active_deadline)
                        api.user.SetForegroundWindow(hwnd)
                        api.wait(lambda: int(api.user.GetForegroundWindow() or 0) == hwnd, 1, active_deadline)
                        for repetition in range(3):
                            if time.monotonic() + 5 >= active_deadline:
                                break
                            row = {"repetition": repetition + 1}
                            row["to_neutral_ack"] = api.keys(hwnd,
                                [(0x12, False), (0x09, False), (0x09, True), (0x12, True)],
                                f"focus.{repetition+1}.to_neutral", active_deadline)
                            row["to_neutral_observed"] = bool(api.wait(
                                lambda: int(api.user.GetForegroundWindow() or 0) == api.neutral, 1, active_deadline))
                            row["foreground_after_out"] = int(api.user.GetForegroundWindow() or 0)
                            if not row["to_neutral_ack"] or not row["to_neutral_observed"]:
                                row["state"] = "blocked_input_or_foreground_transition"
                                focus.append(row)
                                break
                            row["to_product_ack"] = api.keys(api.neutral,
                                [(0x12, False), (0x09, False), (0x09, True), (0x12, True)],
                                f"focus.{repetition+1}.to_product", active_deadline)
                            row["to_product_observed"] = bool(api.wait(
                                lambda: int(api.user.GetForegroundWindow() or 0) == hwnd, 1, active_deadline))
                            row["foreground_after_back"] = int(api.user.GetForegroundWindow() or 0)
                            row["state"] = ("observed" if row["to_product_ack"] and
                                            row["to_product_observed"] else "blocked_input_or_foreground_transition")
                            focus.append(row)
                            if row["state"] != "observed":
                                break
                        if int(api.user.GetForegroundWindow() or 0) == hwnd:
                            origin = api.snapshot(hwnd)["window_rect"]
                            positions = context["workload"]["W03"]["positions"]
                            for monitor in monitors[:3]:
                                if time.monotonic() + 15 >= active_deadline:
                                    break
                                left, top, right, bottom = monitor["work_rect"]
                                width, height = min(1250, right-left-32), min(820, bottom-top-32)
                                if width < 200 or height < 160:
                                    positions.append({"monitor": monitor, "state": "unavailable_work_area"})
                                    continue
                                x, y = left+(right-left-width)//2, top+(bottom-top-height)//2
                                ok = bool(api.user.SetWindowPos(hwnd, None, x, y, width, height, 0x14))
                                api.event("owned_window_position_request", hwnd=hwnd,
                                          monitor=monitor, requested_xywh=[x,y,width,height], returned=ok,
                                          coordinate_scope="observer per-monitor-aware physical pixels")
                                api.wait(lambda: False, .25, active_deadline)
                                positions.append({"monitor": monitor, "request_returned": ok,
                                    "actual": api.snapshot(hwnd),
                                    "capture": api.capture(hwnd, f"monitor{len(positions)+1}")})
                            api.user.SetWindowPos(hwnd, None, origin[0], origin[1],
                                                  origin[2]-origin[0], origin[3]-origin[1], 0x14)
                            api.wait(lambda: False, .2, active_deadline)
                            context["workload"]["W03"]["restored_window"] = api.snapshot(hwnd)
                            context["workload"]["W03"]["state"] = "observed_positions_only"
                        if time.monotonic() + 12 < active_deadline:
                            inventory = uia_inventory(api, process, hwnd, "main-window", active_deadline)
                            context["workload"]["W04"]["main_window_inventory"].append(inventory)
                            context["workload"]["W04"]["state"] = "observed_or_limited_main_window_only"
                    api.wait(lambda: False, .3, active_deadline)
                    attempt["state"] = "observed_external_subset"
                finally:
                    if process:
                        cleanup = close_product(api, process, hwnd, context_terminal)
                        attempt["cleanup"] = cleanup
                        report["cleanup"].append({"context": api.context, "ordinal": ordinal, **cleanup})
                        context["workload"]["W01"]["launches"].append(attempt)
                    for stream in streams:
                        stream.close()
                if attempt.get("cleanup", {}).get("remaining_owned_pids"):
                    raise RuntimeError("Owned product cleanup did not reach terminal membership")
            context["state"] = "observed_partial_workload_not_platform_acceptance"
            context["elapsed_seconds"] = time.monotonic() - context_start
            context["workload"]["W01"]["state"] = "partial_external_observations; inspect each focus/close result"
            context["workload"]["W06"]["state"] = "observed_modules_memory_and_capture_timestamps_only"
            out.json(f"{api.context}/observations.json", context)
        report["state"] = "observed_subset_remaining_scope_pending"
    except BaseException as error:
        report["state"] = "failed"
        report["error"] = repr(error)
        report["traceback"] = traceback.format_exc()
        raise
    finally:
        if api.product:
            emergency = api.product
            report["cleanup"].append({"context": api.context, "emergency": True,
                **close_product(api, emergency, emergency.window(), min(started + 300, time.monotonic() + 10))})
        api.close()
        if old_awareness:
            api.user.SetThreadDpiAwarenessContext(old_awareness)
        report["elapsed_seconds"] = time.monotonic() - started
        report["remaining_scope"] = ["W01 native modal/actual GL", "W02 all interactive field/state checks",
            "W03 repeated moves/fonts/hit targets/mixed-DPI correctness", "W04 remaining states/Narrator",
            "W05 clipboard/native dialog/printing", "W06 navigation/preview/Apply benchmarks/GPU/compiler"]
        out.json("run.failed.json" if report["state"] == "failed" else "run.final.json", report)
        out.json("index.json", {"schema": REPORT_SCHEMA, "members": out.index(),
                                "acceptance_claim": False, "platform_accepted": False})


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--prepare", action="store_true")
    mode.add_argument("--collect", action="store_true")
    mode.add_argument("--_prepare-worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--tool-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prepared", type=Path)
    parser.add_argument("--context", choices=("both", "imgui_cpp", "imgui_python"), default="both")
    args = parser.parse_args()
    if args.collect and not args.prepared:
        parser.error("--collect requires --prepared")
    if not args.collect and args.prepared:
        parser.error("--prepared is only for --collect")
    if len(args.tool_sha256) != 64 or any(ch not in "0123456789abcdefABCDEF" for ch in args.tool_sha256):
        parser.error("--tool-sha256 requires the exact reviewed SHA256")
    return args


def main():
    args = arguments()
    contract, pins = contract_inputs(args.contract, args.tool_sha256)
    if args.collect:
        verify_prepared(args.prepared, contract)
    out = Output(args.output, 64*1024*1024 if not args.collect else MAX_OUTPUT)
    try:
        if args._prepare_worker:
            prepare(contract, pins, out)
        elif args.prepare:
            supervised_prepare(contract, pins, out, args)
        else:
            collect(contract, pins, out, args)
        print(json.dumps({"output": str(out.root), "mode": "collect" if args.collect else "prepare",
                          "acceptance_claim": False, "platform_accepted": False}))
        return 0
    finally:
        out.close()


if __name__ == "__main__":
    raise SystemExit(main())
