"""Run one CI command with Windows Job or Linux subreaper ownership.

``run_owned`` returns the terminal receipt dictionary for an ordinary target
exit, including nonzero exit codes. Surviving descendants are always removed
and reported separately; callers decide whether that outcome is acceptable.
Timeout, launch, ownership and cleanup failures raise OwnedProcessError with
the durable terminal dictionary in ``error.receipt``. No environment values
are serialized. Streams must be inherited, DEVNULL, or real file objects;
PIPE is deliberately unsupported because this helper does not drain output.

This is for the complete CI suite and its installed smoke commands. It does
not change the existing focused-profile protocol. macOS is explicitly refused:
a detached-process containment/registered-spawn contract is still required.
"""
from __future__ import annotations

import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


SCHEMA = "imagejxx.owned-command.001"
CLEANUP_SECONDS = 10.0
SUPERVISOR_GRACE_SECONDS = 10.0
MAX_PROCESSES = 4096
MAX_REQUEST_BYTES = 256 * 1024


class OwnedProcessError(RuntimeError):
    def __init__(self, receipt):
        self.receipt = receipt
        error = receipt.get("error", {})
        super().__init__(error.get("message", "Owned command failed: " + receipt["state"]))


class OwnedProcessTimeout(OwnedProcessError, TimeoutError):
    pass


class OwnedProcessUnsupported(OwnedProcessError, NotImplementedError):
    pass


class _Interrupted(Exception):
    pass


def _encoded(row):
    return (json.dumps(row, indent=2, sort_keys=True, allow_nan=False) + "\n").encode("utf-8")


def _save(path, row, *, create=False):
    """A fresh exclusive receipt, then atomic durable replacements."""
    path = Path(path)
    raw = _encoded(row)
    temporary = path if create else path.with_name(path.name + ".writing")
    opened = False
    try:
        with temporary.open("xb") as stream:
            opened = True
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        if not create:
            os.replace(temporary, path)
    finally:
        # Only our own exact temporary name is removed, without traversal.
        if not create and opened and temporary.exists():
            temporary.unlink()


def _failure(row, state, error):
    row["state"] = state
    row["error"] = {"type": type(error).__name__, "message": str(error)}


def _finish(path, row):
    row["ended"] = time.time()
    row["elapsed_seconds"] = time.monotonic() - row["started_monotonic"]
    _save(path, row)
    return row


def _validate(argv, cwd, env, timeout, stdout, stderr, receipt_path):
    if (not isinstance(argv, (list, tuple)) or not argv or len(argv) > 1024
            or any(not isinstance(value, str) or "\0" in value for value in argv) or not argv[0]
            or sum(len(value.encode("utf-8")) + 1 for value in argv) > 64 * 1024):
        raise ValueError("Owned command requires a bounded nonempty string argument list")
    if (isinstance(timeout, bool) or not isinstance(timeout, (int, float))
            or not math.isfinite(timeout) or not 0 < timeout <= 86400):
        raise ValueError("Owned command timeout must be finite and in (0, 86400]")
    if (not isinstance(env, dict) or any(not isinstance(key, str) or not key or "=" in key
            or "\0" in key or not isinstance(value, str) or "\0" in value
            for key, value in env.items())):
        raise ValueError("Owned command environment must contain valid string pairs")
    for name, stream in (("stdout", stdout), ("stderr", stderr)):
        if stream is None or stream == subprocess.DEVNULL or (name == "stderr" and stream == subprocess.STDOUT):
            continue
        if isinstance(stream, int) or not callable(getattr(stream, "fileno", None)):
            raise ValueError("Owned command streams require real files, inheritance, or DEVNULL")
        if stream.closed or stream.fileno() < 0:
            raise ValueError("Owned command output stream is closed")
    directory = Path(cwd).resolve(strict=True)
    if not directory.is_dir():
        raise ValueError("Owned command working directory must exist")
    receipt = Path(receipt_path).absolute()
    if receipt.exists() or receipt.is_symlink() or receipt.with_name(receipt.name + ".writing").exists():
        raise ValueError("Owned command receipt must be a fresh explicitly owned path")
    if not receipt.parent.is_dir():
        raise ValueError("Owned command receipt parent must already exist")
    return list(argv), directory, receipt


def _new_row(argv, cwd, timeout):
    raw = Path(__file__).read_bytes()
    return {
        "schema": SCHEMA, "platform": sys.platform, "state": "starting",
        "argv": argv, "cwd": str(cwd), "timeout_seconds": float(timeout),
        "cleanup_timeout_seconds": CLEANUP_SECONDS, "started": time.time(),
        "started_monotonic": time.monotonic(), "target_pid": None,
        "target_completed": False, "exit_code": None, "timed_out": False,
        "descendants_remained_after_target_completion": False,
        "cleanup_complete": False, "ownership": {},
        "helper": {"path": str(Path(__file__).resolve()), "bytes": len(raw),
                   "sha256": hashlib.sha256(raw).hexdigest()},
        "cleanup": {"actions": [], "observed": [], "reaped": [], "remaining": []},
    }


def _raise_failure(row):
    if row["state"] == "unsupported":
        raise OwnedProcessUnsupported(row)
    if row["state"] == "timed_out":
        raise OwnedProcessTimeout(row)
    if row["state"] != "exited" or not row["cleanup_complete"]:
        raise OwnedProcessError(row)


def run_owned(argv, *, cwd, env, timeout, stdout, stderr, receipt_path):
    """Run one command, return its terminal row, and prove owned cleanup first.

    An ordinary nonzero exit is returned unchanged. Inspect both ``exit_code``
    and ``descendants_remained_after_target_completion`` for qualification.
    Unsupported hosts fail before any child launch; there is no raw fallback.
    """
    argv, cwd, receipt = _validate(argv, cwd, env, timeout, stdout, stderr, receipt_path)
    row = _new_row(argv, cwd, timeout)
    _save(receipt, row, create=True)
    if sys.platform == "win32":
        row = _run_windows(argv, cwd, env, stdout, stderr, receipt, row)
    elif sys.platform.startswith("linux"):
        row = _run_linux_parent(argv, cwd, env, stdout, stderr, receipt, row)
    else:
        row["cleanup_complete"] = True  # No child was launched.
        row["ownership"] = {"method": "unsupported", "child_launched": False}
        _failure(row, "unsupported", RuntimeError("Owned command containment is unavailable on " + sys.platform))
        _finish(receipt, row)
    _raise_failure(row)
    return row


def _linux_identity(pid):
    """Read only a known owned child, never scan unrelated processes."""
    text = Path(f"/proc/{pid}/stat").read_text()
    tail = text[text.rindex(")") + 2:].split()
    return {"pid": pid, "parent_pid": int(tail[1]), "start_ticks": int(tail[19]), "state": tail[0]}


def _linux_children():
    # This dedicated supervisor never creates another thread. Its sole task's
    # children are exact kernel parentage, including subreaper-adopted orphans.
    path = Path(f"/proc/self/task/{os.getpid()}/children")
    result = [int(value) for value in path.read_text().split()]
    if len(result) > MAX_PROCESSES or any(pid <= 0 or pid == os.getpid() for pid in result):
        raise RuntimeError("Owned child membership exceeds its bound")
    return sorted(set(result))


def _linux_subreaper(owner_pid):
    libc = ctypes.CDLL(None, use_errno=True)
    libc.prctl.restype = ctypes.c_int
    libc.prctl.argtypes = [ctypes.c_int] + [ctypes.c_ulong] * 4
    if type(owner_pid) is not int or owner_pid <= 0:
        raise ValueError("Linux supervisor requires its original owner identity")
    if libc.prctl(1, signal.SIGTERM, 0, 0, 0):  # PR_SET_PDEATHSIG
        raise OSError(ctypes.get_errno(), "Cannot establish Linux owner-death cleanup")
    if os.getppid() != owner_pid:
        raise RuntimeError("Linux owner exited before supervisor admission")
    if libc.prctl(36, 1, 0, 0, 0):  # PR_SET_CHILD_SUBREAPER
        raise OSError(ctypes.get_errno(), "Cannot establish the owned Linux subreaper")
    active = ctypes.c_int()
    if libc.prctl(37, ctypes.addressof(active), 0, 0, 0) or active.value != 1:
        raise RuntimeError("Linux child-subreaper ownership was not established")


def _linux_cleanup(child, row):
    started = time.monotonic()
    deadline = started + CLEANUP_SECONDS
    cleanup = row["cleanup"]
    observed, reaped, signalled = {}, {}, set()
    initial = _linux_children()
    cleanup["initial_direct_children"] = initial
    cleanup["membership_scope"] = "direct/adopted children of the dedicated subreaper"
    row["descendants_remained_after_target_completion"] = bool(row["target_completed"] and initial)

    def retain_exit(pid, status):
        if len(reaped) >= MAX_PROCESSES and pid not in reaped:
            raise RuntimeError("Owned child reap records exceed their bound")
        code = os.waitstatus_to_exitcode(status)
        reaped[pid] = {"pid": pid, "exit_code": code}
        if child is not None and pid == child.pid:
            child.returncode = code
            row["exit_code"] = code

    try:
        while True:
            current = _linux_children()
            for pid in current:
                if pid not in observed:
                    if len(observed) >= MAX_PROCESSES:
                        raise RuntimeError("Owned child identity records exceed their bound")
                    try:
                        identity = _linux_identity(pid)
                    except FileNotFoundError:
                        continue
                    if identity["parent_pid"] != os.getpid():
                        raise RuntimeError("A claimed child is not owned by the Linux supervisor")
                    observed[pid] = identity
                if pid not in signalled:
                    try:
                        os.kill(pid, signal.SIGKILL)
                        cleanup["actions"].append({"action": "SIGKILL", "pid": pid,
                                                   "identity": observed.get(pid)})
                        signalled.add(pid)
                    except ProcessLookupError:
                        pass
            # Only this single-threaded supervisor reaps its children. Until
            # waitpid, an exited child's PID cannot be reused for another owner.
            while True:
                if time.monotonic() >= deadline:
                    raise TimeoutError("Owned Linux child reaping exceeded its cleanup bound")
                try:
                    pid, status = os.waitpid(-1, os.WNOHANG)
                except ChildProcessError:
                    cleanup["terminal_wait"] = "ECHILD"
                    cleanup["remaining"] = []
                    row["cleanup_complete"] = True
                    return
                if pid == 0:
                    break
                retain_exit(pid, status)
            if time.monotonic() >= deadline:
                cleanup["remaining"] = _linux_children()
                raise TimeoutError("Owned Linux children did not all exit within cleanup bound")
            time.sleep(.01)
    finally:
        if not row["cleanup_complete"]:
            try:
                cleanup["remaining"] = _linux_children()
            except BaseException as error:
                cleanup["remaining_unknown"] = True
                cleanup["remaining_error"] = {"type": type(error).__name__, "message": str(error)}
        cleanup["observed"] = [observed[pid] for pid in sorted(observed)]
        cleanup["reaped"] = [reaped[pid] for pid in sorted(reaped)]
        cleanup["signal_count"] = len(signalled)
        cleanup["reaped_count"] = len(reaped)
        cleanup["elapsed_seconds"] = time.monotonic() - started


def _linux_supervisor(request):
    receipt = Path(request["receipt_path"])
    row = request["row"]
    row["ownership"] = {"method": "linux-subreaper", "supervisor_pid": os.getpid(),
                        "caller_pid": request["owner_pid"], "subreaper_established": False,
                        "parent_death_signal": int(signal.SIGTERM)}
    child = None

    def interrupted(signum, frame):
        raise _Interrupted("Owned supervisor received signal " + str(signum))

    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    signal.signal(signal.SIGCHLD, signal.SIG_DFL)
    try:
        _linux_subreaper(request["owner_pid"])
        row["ownership"]["subreaper_established"] = True
        _save(receipt, row)
        try:
            child = subprocess.Popen(row["argv"], cwd=row["cwd"], env=None,
                                     stdin=subprocess.DEVNULL, close_fds=True, start_new_session=True)
        except OSError as error:
            _failure(row, "launch_failed", error)
        else:
            row["target_pid"] = child.pid
            row["ownership"]["target_identity"] = _linux_identity(child.pid)
            row["state"] = "running"
            _save(receipt, row)
            deadline = row["started_monotonic"] + row["timeout_seconds"]
            while True:
                code = child.poll()
                if code is not None:
                    row.update(state="exited", target_completed=True, exit_code=code)
                    break
                if time.monotonic() >= deadline:
                    row["timed_out"] = True
                    _failure(row, "timed_out", TimeoutError("Owned command exceeded its timeout"))
                    break
                time.sleep(min(.025, max(0., deadline - time.monotonic())))
    except BaseException as error:
        _failure(row, "ownership_failed" if child is None else "supervisor_failed", error)
    finally:
        # Ignore repeated external interrupts only during the bounded cleanup.
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        try:
            _linux_cleanup(child, row)
        except BaseException as error:
            row["prior_error"] = row.get("error")
            _failure(row, "cleanup_failed", error)
        _finish(receipt, row)


def _run_linux_parent(argv, cwd, env, stdout, stderr, receipt, row):
    request = _encoded({"receipt_path": str(receipt), "row": row, "owner_pid": os.getpid()})
    if len(request) > MAX_REQUEST_BYTES:
        _failure(row, "launch_failed", ValueError("Owned supervisor request exceeds its byte bound"))
        row["cleanup_complete"] = True
        return _finish(receipt, row)
    supervisor = None
    try:
        supervisor = subprocess.Popen([sys.executable, "-I", "-B", str(Path(__file__).resolve()),
                                       "--linux-supervisor"], cwd=cwd, env=env,
                                      stdin=subprocess.PIPE, stdout=stdout, stderr=stderr, close_fds=True)
        # communicate applies its deadline to writing the bounded request too.
        supervisor.communicate(request, timeout=row["timeout_seconds"] + CLEANUP_SECONDS + SUPERVISOR_GRACE_SECONDS)
        observed = json.loads(receipt.read_bytes())
        if (observed.get("schema") != SCHEMA or observed.get("argv") != argv
                or observed.get("ownership", {}).get("supervisor_pid") != supervisor.pid
                or supervisor.returncode != 0 or observed.get("state") in ("starting", "running")):
            raise RuntimeError("Owned Linux supervisor did not produce a valid terminal receipt")
        return observed
    except BaseException as error:
        # Ask the *retained own supervisor* to perform its cleanup. Killing it
        # outright could abandon adopted descendants; never call that success.
        if supervisor is not None and supervisor.poll() is None:
            supervisor.terminate()
            try:
                supervisor.wait(timeout=CLEANUP_SECONDS + 1.)
            except subprocess.TimeoutExpired:
                pass
        try:
            observed = json.loads(receipt.read_bytes())
            if observed.get("schema") == SCHEMA:
                row = observed
        except (OSError, ValueError):
            pass
        state = "launch_failed" if supervisor is None else "supervisor_failed"
        _failure(row, state, error)
        row["supervisor_exit_code"] = None if supervisor is None else supervisor.poll()
        row["supervisor_pid_if_unresolved"] = (
            supervisor.pid if supervisor is not None and supervisor.poll() is None else None)
        if supervisor is None:
            row["cleanup_complete"] = True
        return _finish(receipt, row)


class _WindowsJob:
    """Kernel-only extraction of the existing suspended JobProcess seam.

    No observer UI, process-name search, taskkill, or historical build paths.
    The job is established before the target's first user instruction.
    """
    def __init__(self):
        from ctypes import wintypes as W
        self.W = W
        C = ctypes
        PTR = C.c_size_t

        class Startup(C.Structure):
            _fields_ = [("cb", W.DWORD), ("reserved", W.LPWSTR), ("desktop", W.LPWSTR),
                        ("title", W.LPWSTR)] + [(name, W.DWORD) for name in (
                            "x", "y", "x_size", "y_size", "x_chars", "y_chars", "fill", "flags")] + [
                        ("show", W.WORD), ("reserved_bytes", W.WORD), ("reserved_data", C.c_void_p),
                        ("stdin", W.HANDLE), ("stdout", W.HANDLE), ("stderr", W.HANDLE)]

        class Process(C.Structure):
            _fields_ = [("process", W.HANDLE), ("thread", W.HANDLE), ("pid", W.DWORD), ("tid", W.DWORD)]

        class Basic(C.Structure):
            _fields_ = [("process_time", C.c_int64), ("job_time", C.c_int64), ("flags", W.DWORD),
                        ("min_ws", PTR), ("max_ws", PTR), ("active_limit", W.DWORD),
                        ("affinity", PTR), ("priority", W.DWORD), ("scheduling", W.DWORD)]

        class Io(C.Structure):
            _fields_ = [(name, C.c_uint64) for name in ("read_ops", "write_ops", "other_ops",
                                                       "read_bytes", "write_bytes", "other_bytes")]

        class Limits(C.Structure):
            _fields_ = [("basic", Basic), ("io", Io), ("process_memory", PTR), ("job_memory", PTR),
                        ("peak_process_memory", PTR), ("peak_job_memory", PTR)]

        class Members(C.Structure):
            _fields_ = [("assigned", W.DWORD), ("count", W.DWORD), ("pids", PTR * MAX_PROCESSES)]

        self.Startup, self.Process, self.Limits, self.Members = Startup, Process, Limits, Members
        self.kernel = C.WinDLL("kernel32", use_last_error=True, winmode=0x800)
        k = self.kernel

        def proto(name, restype, args):
            method = getattr(k, name)
            method.restype, method.argtypes = restype, args

        proto("CreateJobObjectW", W.HANDLE, [C.c_void_p, W.LPCWSTR])
        proto("SetInformationJobObject", W.BOOL, [W.HANDLE, C.c_int, C.c_void_p, W.DWORD])
        proto("QueryInformationJobObject", W.BOOL, [W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.c_void_p])
        proto("AssignProcessToJobObject", W.BOOL, [W.HANDLE, W.HANDLE])
        proto("IsProcessInJob", W.BOOL, [W.HANDLE, W.HANDLE, C.POINTER(W.BOOL)])
        proto("TerminateJobObject", W.BOOL, [W.HANDLE, W.UINT])
        proto("TerminateProcess", W.BOOL, [W.HANDLE, W.UINT])
        proto("WaitForSingleObject", W.DWORD, [W.HANDLE, W.DWORD])
        proto("GetExitCodeProcess", W.BOOL, [W.HANDLE, C.POINTER(W.DWORD)])
        proto("GetProcessTimes", W.BOOL, [W.HANDLE] + [C.POINTER(W.FILETIME)] * 4)
        proto("OpenProcess", W.HANDLE, [W.DWORD, W.BOOL, W.DWORD])
        proto("CloseHandle", W.BOOL, [W.HANDLE])
        proto("GetCurrentProcess", W.HANDLE, [])
        proto("GetStdHandle", W.HANDLE, [W.DWORD])
        proto("DuplicateHandle", W.BOOL, [W.HANDLE, W.HANDLE, W.HANDLE, C.POINTER(W.HANDLE),
                                          W.DWORD, W.BOOL, W.DWORD])
        proto("ResumeThread", W.DWORD, [W.HANDLE])
        proto("CreateProcessW", W.BOOL, [W.LPCWSTR, W.LPWSTR, C.c_void_p, C.c_void_p,
                                         W.BOOL, W.DWORD, C.c_void_p, W.LPCWSTR,
                                         C.POINTER(Startup), C.POINTER(Process)])
        self.job = None
        self.process = None
        self.pid = None
        self.assigned = False
        self.handles = {}

    def require(self, result):
        if not result:
            raise ctypes.WinError(ctypes.get_last_error())
        return result

    def identity(self, handle, pid):
        values = [self.W.FILETIME() for _ in range(4)]
        self.require(self.kernel.GetProcessTimes(handle, *(ctypes.byref(value) for value in values)))
        return {"pid": pid, "creation_filetime": (values[0].dwHighDateTime << 32) | values[0].dwLowDateTime}

    def launch(self, argv, cwd, env, stdout, stderr, row):
        import msvcrt
        C, W, k = ctypes, self.W, self.kernel
        self.job = self.require(k.CreateJobObjectW(None, None))
        limits = self.Limits()
        limits.basic.flags = 0x2000  # KILL_ON_JOB_CLOSE; neither breakaway flag.
        self.require(k.SetInformationJobObject(self.job, 9, C.byref(limits), C.sizeof(limits)))
        owned_files, duplicates = [], []

        def file_handle(stream, standard):
            if stream is None:
                handle = k.GetStdHandle(standard & 0xffffffff)
                if handle and handle != C.c_void_p(-1).value:
                    return handle
                stream = subprocess.DEVNULL
            if stream == subprocess.DEVNULL:
                stream = open(os.devnull, "wb")
                owned_files.append(stream)
            return msvcrt.get_osfhandle(stream.fileno())

        def duplicate(handle):
            result = W.HANDLE()
            current = k.GetCurrentProcess()
            self.require(k.DuplicateHandle(current, handle, current, C.byref(result), 0, True, 2))
            duplicates.append(result.value)
            return result.value

        pi = self.Process()
        try:
            null = open(os.devnull, "rb")
            owned_files.append(null)
            info = self.Startup()
            info.cb = C.sizeof(info)
            info.flags = 0x100  # STARTF_USESTDHANDLES.
            info.stdin = duplicate(msvcrt.get_osfhandle(null.fileno()))
            info.stdout = duplicate(file_handle(stdout, -11))
            info.stderr = info.stdout if stderr == subprocess.STDOUT else duplicate(file_handle(stderr, -12))
            command = subprocess.list2cmdline(argv)
            if len(command) >= 32767:
                raise ValueError("Windows command line exceeds the native limit")
            environment = C.create_unicode_buffer("\0".join(
                key + "=" + value for key, value in sorted(env.items(), key=lambda item: item[0].upper())) + "\0\0")
            self.require(k.CreateProcessW(argv[0], C.create_unicode_buffer(command), None, None, True,
                                          0x4 | 0x400 | 0x08000000, environment, str(cwd),
                                          C.byref(info), C.byref(pi)))
            self.process, self.pid = pi.process, int(pi.pid)
            self.handles[self.pid] = self.process
            row["target_pid"] = self.pid
            row["ownership"]["target_identity"] = self.identity(self.process, self.pid)
            self.require(k.AssignProcessToJobObject(self.job, self.process))
            self.assigned = True
            row["ownership"]["assigned_before_resume"] = True
            if k.ResumeThread(pi.thread) == 0xffffffff:
                raise C.WinError(C.get_last_error())
            row["ownership"]["resumed"] = True
        finally:
            if pi.thread:
                k.CloseHandle(pi.thread)
            for handle in duplicates:
                k.CloseHandle(handle)
            for stream in owned_files:
                stream.close()

    def completed(self):
        result = self.kernel.WaitForSingleObject(self.process, 0)
        if result == 0:
            return True
        if result != 258:  # WAIT_TIMEOUT, not process exit status 259.
            raise ctypes.WinError(ctypes.get_last_error())
        return False

    def exit_code(self, handle=None):
        value = self.W.DWORD()
        self.require(self.kernel.GetExitCodeProcess(self.process if handle is None else handle, ctypes.byref(value)))
        return int(value.value)

    def members(self):
        values = self.Members()
        self.require(self.kernel.QueryInformationJobObject(
            self.job, 3, ctypes.byref(values), ctypes.sizeof(values), None))
        if values.assigned > MAX_PROCESSES or values.count > MAX_PROCESSES:
            raise RuntimeError("Owned Windows job membership exceeds its bound")
        return sorted({int(values.pids[index]) for index in range(values.count)})

    def member_identity(self, pid):
        if pid not in self.handles:
            handle = self.kernel.OpenProcess(0x1000 | 0x100000, False, pid)
            if not handle:
                return {"pid": pid, "identity_state": "exited_before_handle_acquisition"}
            member = self.W.BOOL()
            try:
                self.require(self.kernel.IsProcessInJob(handle, self.job, ctypes.byref(member)))
                if not member.value:
                    return {"pid": pid, "identity_state": "membership_changed_before_handle_acquisition"}
                self.handles[pid] = handle
            finally:
                if pid not in self.handles:
                    self.kernel.CloseHandle(handle)
        return self.identity(self.handles[pid], pid)

    def cleanup(self, row):
        started = time.monotonic()
        deadline = started + CLEANUP_SECONDS
        cleanup = row["cleanup"]
        try:
            current = self.members() if self.job else []
            cleanup["initial_job_members"] = current
            cleanup["membership_scope"] = "all members of the retained Windows Job"
            cleanup["observed"] = [self.member_identity(pid) for pid in current]
            row["descendants_remained_after_target_completion"] = bool(
                row["target_completed"] and any(pid != self.pid for pid in current))
            if current:
                self.require(self.kernel.TerminateJobObject(self.job, 1))
                cleanup["actions"].append({"action": "TerminateJobObject", "observed_members": current})
            if self.process and not self.assigned and not self.completed():
                # Assignment failure leaves a suspended child. It must be
                # terminated directly while its exact retained handle is owned.
                self.require(self.kernel.TerminateProcess(self.process, 1))
                cleanup["actions"].append({"action": "TerminateProcess", "pid": self.pid,
                                           "reason": "suspended_unassigned_child"})
            while True:
                remaining = self.members() if self.job else []
                target_done = not self.process or self.completed()
                cleanup["remaining"] = remaining
                if not remaining and target_done:
                    break
                if time.monotonic() >= deadline:
                    raise TimeoutError("Owned Windows Job did not empty within cleanup bound")
                time.sleep(.01)
            if self.process and row["exit_code"] is None:
                row["exit_code"] = self.exit_code()
            waited = []
            for pid, handle in self.handles.items():
                while self.kernel.WaitForSingleObject(handle, 0) != 0:
                    if time.monotonic() >= deadline:
                        raise TimeoutError("Owned Windows member handle did not signal within cleanup bound")
                    time.sleep(.01)
                waited.append({"pid": pid, "exit_code": self.exit_code(handle)})
            cleanup["waited"] = waited
            cleanup["waited_count"] = len(waited)
            cleanup["terminal_wait"] = "job_empty_and_target_signalled"
            cleanup["terminated_job_count"] = sum(action["action"] == "TerminateJobObject" for action in cleanup["actions"])
            row["cleanup_complete"] = True
        finally:
            cleanup["elapsed_seconds"] = time.monotonic() - started

    def close(self):
        # Final kernel backstop even when receipt/query/cleanup itself fails.
        if self.job:
            self.require(self.kernel.CloseHandle(self.job))
            self.job = None
        for handle in self.handles.values():
            self.require(self.kernel.CloseHandle(handle))
        self.handles.clear()
        self.process = None


def _run_windows(argv, cwd, env, stdout, stderr, receipt, row):
    job = None
    row["ownership"] = {"method": "windows-job", "supervisor_pid": os.getpid(),
                        "kill_on_close": True, "breakaway_allowed": False,
                        "assigned_before_resume": False, "resumed": False}
    try:
        job = _WindowsJob()
        job.launch(argv, cwd, env, stdout, stderr, row)
        row["state"] = "running"
        _save(receipt, row)
        deadline = row["started_monotonic"] + row["timeout_seconds"]
        while True:
            if job.completed():
                row.update(state="exited", target_completed=True, exit_code=job.exit_code())
                break
            if time.monotonic() >= deadline:
                row["timed_out"] = True
                _failure(row, "timed_out", TimeoutError("Owned command exceeded its timeout"))
                break
            time.sleep(min(.025, max(0., deadline - time.monotonic())))
    except BaseException as error:
        state = "launch_failed" if job is None or not job.process else "ownership_failed"
        _failure(row, state, error)
    finally:
        try:
            if job is not None:
                job.cleanup(row)
            else:
                row["cleanup_complete"] = True
        except BaseException as error:
            row["prior_error"] = row.get("error")
            _failure(row, "cleanup_failed", error)
        finally:
            if job is not None:
                try:
                    job.close()
                    row["ownership"]["handles_closed"] = True
                except BaseException as error:
                    row["cleanup_complete"] = False
                    row["prior_error"] = row.get("error")
                    _failure(row, "cleanup_failed", error)
            _finish(receipt, row)
    return row


if __name__ == "__main__":
    if sys.argv[1:] != ["--linux-supervisor"] or not sys.platform.startswith("linux"):
        raise SystemExit("This helper has no standalone unowned-command mode")
    payload = sys.stdin.buffer.read(MAX_REQUEST_BYTES + 1)
    if len(payload) > MAX_REQUEST_BYTES:
        raise SystemExit("Owned supervisor request exceeds its byte bound")
    _linux_supervisor(json.loads(payload))
