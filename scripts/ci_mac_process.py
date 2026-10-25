"""Cooperative macOS ownership for the trusted complete Python CI suite.

The broker is a dedicated session leader and the parent of every registered
target. A target cannot exec until a separate broker-owned anchor has joined
its new process group. The broker never reaps that anchor until the group has
drained. All signalling addresses these pinned groups, never sampled descendant
PIDs. Only PROC_PGRP_ONLY queries are used, not an ancestry or machine-wide scan.

This is NOT containment of arbitrary untrusted code. The audited suite routes
its five detached Popen sites through spawn_registered; its other subprocesses
inherit their registered group. A new setsid/setpgid/daemon route must first be
registered and reviewed. Standalone C++ observation capture is a separate owner.
Missing registration, unsupported Popen options and non-Darwin execution refuse;
there is no unowned fallback. Actual Darwin lifecycle tests remain required.

The private socket carries bounded authenticated requests and SCM_RIGHTS copies
of the caller's actual streams. No environment values or credentials enter the
durable receipt. run_mac_owned has run_owned's keyword/terminal-row contract;
its errors carry .receipt. Proxies expose only pid, returncode, poll, wait and
terminate_tree. start_new_session=True at the audited proxy sites requests an
owned group within the broker's session, not another untracked session.
"""
from __future__ import annotations

import array
import ctypes
import hashlib
import hmac
import importlib.util
import json
import math
import os
from pathlib import Path
import secrets
import select
import signal
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time


_COMMON_PATH = Path(__file__).with_name("ci_owned_process.py")
_COMMON_SPEC = importlib.util.spec_from_file_location("_imagejxx_mac_owned_common", _COMMON_PATH)
_common = importlib.util.module_from_spec(_COMMON_SPEC)
_COMMON_SPEC.loader.exec_module(_common)
OwnedProcessError = _common.OwnedProcessError
OwnedProcessTimeout = _common.OwnedProcessTimeout
OwnedProcessUnsupported = _common.OwnedProcessUnsupported

SCHEMA = _common.SCHEMA
WIRE_SCHEMA = "imagejxx.mac-spawn.001"
SOCKET_ENV = "IMAGEJXX_MAC_BROKER_SOCKET"
TOKEN_ENV = "IMAGEJXX_MAC_BROKER_TOKEN"
MAX_MESSAGE_BYTES = 256 * 1024
MAX_GROUPS = 128
MAX_PROCESSES = 4096
MAX_FDS = 3
BOOTSTRAP_SECONDS = 5.0
RPC_SECONDS = 20.0
CLEANUP_SECONDS = 10.0
POLL_SECONDS = .025


def _closed_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError("Duplicate macOS ownership protocol key")
        value[key] = item
    return value


def _encode(value):
    raw = json.dumps(value, ensure_ascii=True, separators=(",", ":"), allow_nan=False).encode("utf-8")
    if not 0 < len(raw) <= MAX_MESSAGE_BYTES:
        raise ValueError("macOS ownership protocol byte bound")
    return raw


def _decode(raw):
    def invalid_constant(value):
        raise ValueError("Nonfinite macOS ownership protocol value")
    value = json.loads(raw, object_pairs_hook=_closed_object, parse_constant=invalid_constant)
    if not isinstance(value, dict):
        raise ValueError("macOS ownership protocol requires an object")
    return value


def _send(channel, value, fds=()):
    raw = _encode(value)
    if len(fds) > MAX_FDS:
        raise ValueError("macOS ownership descriptor bound")
    packet = struct.pack("!I", len(raw)) + raw
    ancillary = [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", fds))] if fds else []
    sent = channel.sendmsg([packet], ancillary)
    if sent <= 0:
        raise ConnectionError("macOS ownership channel did not send")
    if sent < len(packet):
        channel.sendall(packet[sent:])


def _receive(channel, *, allow_eof=False):
    fds = []
    try:
        header, ancillary, flags, _ = channel.recvmsg(4, socket.CMSG_SPACE(MAX_FDS * array.array("i").itemsize))
        for level, kind, data in ancillary:
            if level != socket.SOL_SOCKET or kind != socket.SCM_RIGHTS:
                raise ValueError("Unsupported macOS ownership ancillary message")
            values = array.array("i")
            if len(data) % values.itemsize:
                raise ValueError("Invalid macOS ownership descriptor payload")
            values.frombytes(data)
            fds.extend(values)
        if flags & (getattr(socket, "MSG_CTRUNC", 0) | getattr(socket, "MSG_TRUNC", 0)) or len(fds) > MAX_FDS:
            raise ValueError("Truncated macOS ownership message")
        if not header and allow_eof and not fds:
            return None, []
        while len(header) < 4:
            block = channel.recv(4 - len(header))
            if not block:
                raise ConnectionError("Incomplete macOS ownership header")
            header += block
        count = struct.unpack("!I", header)[0]
        if not 0 < count <= MAX_MESSAGE_BYTES:
            raise ValueError("macOS ownership protocol byte bound")
        parts, left = [], count
        while left:
            block = channel.recv(left)
            if not block:
                raise ConnectionError("Incomplete macOS ownership payload")
            parts.append(block)
            left -= len(block)
        return _decode(b"".join(parts)), fds
    except BaseException:
        for fd in fds:
            os.close(fd)
        raise


def _argv(value):
    if (not isinstance(value, (tuple, list)) or not value or len(value) > 1024
            or any(not isinstance(item, str) or "\0" in item for item in value) or not value[0]
            or sum(len(item.encode("utf-8")) + 1 for item in value) > 64 * 1024):
        raise ValueError("Registered command requires a bounded nonempty string argument list")
    return list(value)


def _environment(value):
    if (not isinstance(value, dict) or any(not isinstance(key, str) or not key or "=" in key or "\0" in key
            or not isinstance(item, str) or "\0" in item for key, item in value.items())):
        raise ValueError("Registered environment must contain valid string pairs")
    return dict(value)


def _spawn_options(argv, kwargs):
    allowed = {"cwd", "env", "stdin", "stdout", "stderr", "close_fds", "start_new_session"}
    if set(kwargs) - allowed or kwargs.get("close_fds", True) is not True:
        raise ValueError("Unsupported registered Popen option")
    if kwargs.get("start_new_session") is not True:
        raise ValueError("Registered spawn requires the audited detached-launch intent")
    directory = Path(kwargs.get("cwd") or os.getcwd()).resolve(strict=True)
    if not directory.is_dir():
        raise ValueError("Registered working directory must exist")
    environment = _environment(os.environ.copy() if kwargs.get("env") is None else kwargs["env"])
    descriptors, streams = [], {}
    for index, name in enumerate(("stdin", "stdout", "stderr")):
        stream = kwargs.get(name)
        if stream == subprocess.DEVNULL:
            streams[name] = "null"
        elif name == "stderr" and stream == subprocess.STDOUT:
            streams[name] = "stdout"
        else:
            if stream is None:
                fd = index
            elif isinstance(stream, int) or not callable(getattr(stream, "fileno", None)) or stream.closed:
                raise ValueError("Registered streams require real files, inheritance, or DEVNULL")
            else:
                fd = stream.fileno()
            if fd < 0:
                raise ValueError("Registered stream is closed")
            os.fstat(fd)
            streams[name] = len(descriptors)
            descriptors.append(fd)
    return {"argv": _argv(argv), "cwd": str(directory), "env": environment, "streams": streams}, descriptors


def _credentials():
    if sys.platform != "darwin":
        raise RuntimeError("Registered macOS spawn is unavailable on this platform")
    address, token = os.environ.get(SOCKET_ENV), os.environ.get(TOKEN_ENV)
    if (not address or not isinstance(token, str) or len(token) != 64
            or any(letter not in "0123456789abcdef" for letter in token)):
        raise RuntimeError("macOS child launch has no registered broker")
    path = Path(address)
    parent = path.parent.lstat()
    endpoint = path.lstat()
    if (not path.is_absolute() or len(os.fsencode(address)) >= 104
            or not stat.S_ISDIR(parent.st_mode) or parent.st_uid != os.getuid()
            or stat.S_IMODE(parent.st_mode) != 0o700 or not stat.S_ISSOCK(endpoint.st_mode)):
        raise RuntimeError("macOS broker endpoint is not a private owned socket")
    return address, token


def _rpc(address, token, operation, *, fds=()):
    request = {"schema": WIRE_SCHEMA, "token": token, **operation}
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as channel:
        channel.settimeout(RPC_SECONDS)
        channel.connect(address)
        _send(channel, request, fds)
        response, received = _receive(channel)
    for fd in received:
        os.close(fd)
    if received or set(response) not in ({"ok", "result"}, {"ok", "error"}):
        raise RuntimeError("Invalid registered spawn response")
    if response["ok"] is not True:
        raise RuntimeError("Registered spawn refused: " + response["error"])
    return response["result"]


class RegisteredProcess:
    """Narrow Popen-shaped client; only the broker creates or reaps the PID."""
    def __init__(self, address, token, value, argv):
        if (not isinstance(value, dict) or set(value) != {"pid", "group_id", "returncode"}
                or type(value["pid"]) is not int or value["pid"] <= 0
                or not isinstance(value["group_id"], str) or len(value["group_id"]) != 32
                or value["returncode"] is not None):
            raise RuntimeError("Invalid registered process identity")
        self.pid, self.args = value["pid"], list(argv)
        self.returncode = None
        self._address, self._token, self._group = address, token, value["group_id"]
        self._cleanup = None

    def poll(self):
        if self.returncode is None:
            value = _rpc(self._address, self._token,
                         {"op": "poll", "group_id": self._group, "pid": self.pid})
            if value is not None and type(value) is not int:
                raise RuntimeError("Invalid registered process return code")
            self.returncode = value
        return self.returncode

    def wait(self, timeout=None):
        if timeout is not None and (isinstance(timeout, bool) or not isinstance(timeout, (int, float))
                or not math.isfinite(timeout) or timeout < 0):
            raise ValueError("Registered wait timeout must be finite and nonnegative")
        deadline = None if timeout is None else time.monotonic() + timeout
        while True:
            code = self.poll()
            if code is not None:
                return code
            if deadline is not None and time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired(self.args, timeout)
            time.sleep(POLL_SECONDS if deadline is None else min(POLL_SECONDS, max(0., deadline - time.monotonic())))

    def terminate_tree(self):
        if self._cleanup is None:
            value = _rpc(self._address, self._token,
                         {"op": "terminate", "group_id": self._group, "pid": self.pid})
            if (not isinstance(value, dict) or value.get("cleanup_complete") is not True
                    or type(value.get("exit_code")) is not int or value.get("launcher_reaped") is not True):
                raise RuntimeError("Registered process cleanup did not complete")
            self.returncode, self._cleanup = value["exit_code"], value
        return dict(self._cleanup)


def spawn_registered(argv, **kwargs):
    """Launch one audited detached child through the active Darwin broker."""
    address, token = _credentials()
    request, fds = _spawn_options(argv, kwargs)
    value = _rpc(address, token, {"op": "spawn", **request}, fds=fds)
    return RegisteredProcess(address, token, value, request["argv"])


class _DarwinGroups:
    def __init__(self):
        if sys.platform != "darwin":
            raise RuntimeError("Darwin group observation requires the actual Darwin kernel")
        self.library = ctypes.CDLL("/usr/lib/libproc.dylib", use_errno=True)
        self.listpids = self.library.proc_listpids
        # Official XNU bsd/sys/proc_info.h: PROC_PGRP_ONLY=2; prototype in
        # libsyscall/wrappers/libproc/libproc.h. No PROC_ALL_PIDS calls.
        self.listpids.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_int]
        self.listpids.restype = ctypes.c_int

    def members(self, pgid):
        if type(pgid) is not int or pgid <= 0:
            raise ValueError("Owned group requires a positive PGID")
        buffer = (ctypes.c_int * (MAX_PROCESSES + 1))()
        ctypes.set_errno(0)
        count = self.listpids(2, pgid, buffer, ctypes.sizeof(buffer))
        error = ctypes.get_errno()
        if count < 0 or (count == 0 and error):
            raise OSError(error, "Cannot inspect the registered process group")
        if count % ctypes.sizeof(ctypes.c_int) or count >= ctypes.sizeof(buffer):
            raise RuntimeError("Owned process-group membership exceeds its bound")
        values = [int(buffer[index]) for index in range(count // ctypes.sizeof(ctypes.c_int))]
        if any(pid <= 0 for pid in values) or len(values) != len(set(values)):
            raise RuntimeError("Invalid owned process-group membership")
        return sorted(values)


def _ipc_directory_identity(directory):
    info = directory.lstat()
    if not stat.S_ISDIR(info.st_mode):
        raise RuntimeError("Owned broker directory was replaced by another node")
    return (info.st_dev, info.st_ino)


def _retire_ipc(directory, expected_identity):
    """Remove only our socket and empty original directory, without traversal."""
    try:
        identity = _ipc_directory_identity(directory)
    except FileNotFoundError:
        return
    if identity != tuple(expected_identity):
        raise RuntimeError("Owned broker directory identity changed")
    endpoint = directory / "broker.sock"
    try:
        endpoint_info = endpoint.lstat()
    except FileNotFoundError:
        pass
    else:
        if not stat.S_ISSOCK(endpoint_info.st_mode):
            raise RuntimeError("Owned broker socket was replaced by another node")
        endpoint.unlink()
    directory.rmdir()


def _command_pin(argv):
    raw = json.dumps(argv, ensure_ascii=True, separators=(",", ":")).encode("utf-8")
    return {"executable": argv[0], "argument_count": len(argv), "argv_sha256": hashlib.sha256(raw).hexdigest()}


class _Group:
    def __init__(self, child, anchor, anchor_channel, backend, argv, group_id):
        self.child, self.anchor, self.anchor_channel, self.backend = child, anchor, anchor_channel, backend
        self.pgid, self.group_id = child.pid, group_id
        self.record = {"pid": child.pid, "pgid": child.pid, "anchor_pid": anchor.pid,
                       **_command_pin(argv), "exec_released": False, "target_completed": False,
                       "exit_code": None, "cleanup_complete": False,
                       "descendants_remained_after_target_completion": False,
                       "cleanup": {"actions": [], "observed": [], "remaining": [], "reaped": []}}

    def poll(self):
        if not self.record["cleanup_complete"]:
            code = self.child.poll()
            if code is not None:
                self.record.update(target_completed=True, exit_code=code)
                self.cleanup()
        return self.record["exit_code"]

    def cleanup(self, deadline=None):
        if self.record["cleanup_complete"]:
            return
        started = time.monotonic()
        deadline = min(started + CLEANUP_SECONDS, deadline) if deadline is not None else started + CLEANUP_SECONDS
        cleanup = self.record["cleanup"]
        observed = set()
        # Never poll/wait anchor here. XNU keeps zombie group membership until
        # reap_child_locked -> pgrp_leave_locked (bsd/kern/kern_exit.c). Even
        # after SIGKILL, this unreaped direct child pins every killpg/query.
        first = self.backend.members(self.pgid)
        cleanup["initial_members"] = first
        observed.update(first)
        self.record["descendants_remained_after_target_completion"] = bool(
            self.record["target_completed"] and set(first) - {self.anchor.pid, self.child.pid})
        try:
            while True:
                current = self.backend.members(self.pgid)
                observed.update(current)
                if len(observed) > MAX_PROCESSES:
                    raise RuntimeError("Owned group observation exceeds its bound")
                action = cleanup["actions"][0] if cleanup["actions"] else {
                    "action": "SIGKILL_group", "pgid": self.pgid, "attempts": 0, "successful_sends": 0}
                if not cleanup["actions"]:
                    cleanup["actions"].append(action)
                action["attempts"] += 1
                try:
                    os.killpg(self.pgid, signal.SIGKILL)
                    action["successful_sends"] += 1
                except (ProcessLookupError, PermissionError) as error:
                    # Darwin may report no signalable processes when only the
                    # unreaped anchor/zombies remain. Never count that as a
                    # successful send: only observed drain can finish cleanup.
                    action["last_error"] = {"type": type(error).__name__, "errno": error.errno}
                # The target may be reaped: the separate anchor stays pinned.
                code = self.child.poll()
                if code is not None:
                    self.record["exit_code"] = code
                remaining = self.backend.members(self.pgid)
                observed.update(remaining)
                cleanup["remaining"] = sorted(set(remaining) - {self.anchor.pid})
                if not cleanup["remaining"] and code is not None:
                    break
                if time.monotonic() >= deadline:
                    raise TimeoutError("Registered macOS group did not drain within cleanup bound")
                time.sleep(.01)
            anchor_code = self.anchor.wait(timeout=max(.001, deadline - time.monotonic()))
            cleanup["reaped"] = [{"pid": self.child.pid, "exit_code": self.record["exit_code"]},
                                  {"pid": self.anchor.pid, "exit_code": anchor_code}]
            cleanup["terminal_wait"] = "group_drained_before_anchor_reap"
            self.record["cleanup_complete"] = True
        finally:
            cleanup["observed"] = sorted(observed)
            cleanup["elapsed_seconds"] = time.monotonic() - started
            if self.record["cleanup_complete"]:
                self.anchor_channel.close()


def _child_bootstrap(role, descriptor, pgid=None):
    if sys.platform != "darwin":
        raise RuntimeError("Group bootstrap requires Darwin")
    signal.pthread_sigmask(signal.SIG_UNBLOCK, {signal.SIGTERM, signal.SIGINT})
    with socket.socket(fileno=descriptor) as channel:
        os.set_inheritable(descriptor, False)
        os.setpgid(0, 0 if role == "target" else pgid)
        _send(channel, {"pid": os.getpid(), "pgid": os.getpgrp(), "sid": os.getsid(0)})
        if role == "anchor":
            try:
                while channel.recv(1):
                    pass
            finally:
                # Broker death/closed control is a fail-safe, not a receipt of
                # complete cleanup. This anchor is itself a live group member.
                os.killpg(os.getpgrp(), signal.SIGKILL)
        else:
            request, fds = _receive(channel)
            for fd in fds:
                os.close(fd)
            if fds or set(request) != {"exec"}:
                raise ValueError("Invalid gated target request")
            argv = _argv(request["exec"])
            try:
                os.execvpe(argv[0], argv, os.environ)
            except BaseException as error:
                _send(channel, {"error": {"type": type(error).__name__, "message": str(error)}})
                raise SystemExit(127)


def _registered_popen(on_process, role, *args, **kwargs):
    # A Python signal handler must not interrupt between Popen returning an
    # owned PID and registering its retained object. Bootstrap clears this mask
    # before any user exec; SIGKILL remains unaffected throughout.
    previous = signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGTERM, signal.SIGINT})
    try:
        process = subprocess.Popen(*args, **kwargs)
        on_process(process, role)
        return process
    finally:
        signal.pthread_sigmask(signal.SIG_SETMASK, previous)


def _launch_group(argv, cwd, environment, streams, backend, group_id, on_process, on_group):
    target_parent, target_child = socket.socketpair()
    target_parent.settimeout(BOOTSTRAP_SECONDS)
    anchor_parent = anchor_child = child = anchor = group = None
    try:
        child = _registered_popen(on_process, "gated_target", [sys.executable, "-I", "-B", str(Path(__file__).resolve()),
                                  "--target", str(target_child.fileno())], cwd=cwd, env=environment,
                                 pass_fds=(target_child.fileno(),), close_fds=True, **streams)
        target_child.close()
        identity, fds = _receive(target_parent)
        for fd in fds:
            os.close(fd)
        if fds or identity != {"pid": child.pid, "pgid": child.pid, "sid": os.getsid(0)}:
            raise RuntimeError("Gated target did not establish its owned group")
        anchor_parent, anchor_child = socket.socketpair()
        anchor_parent.settimeout(BOOTSTRAP_SECONDS)
        anchor = _registered_popen(on_process, "gated_anchor", [sys.executable, "-I", "-B", str(Path(__file__).resolve()),
                                   "--anchor", str(anchor_child.fileno()), str(child.pid)], cwd=cwd,
                                  env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL, pass_fds=(anchor_child.fileno(),), close_fds=True)
        anchor_child.close()
        identity, fds = _receive(anchor_parent)
        for fd in fds:
            os.close(fd)
        if fds or identity != {"pid": anchor.pid, "pgid": child.pid, "sid": os.getsid(0)}:
            raise RuntimeError("Anchor did not join the gated target group")
        group = _Group(child, anchor, anchor_parent, backend, argv, group_id)
        on_group(group)
        if not {child.pid, anchor.pid}.issubset(backend.members(child.pid)):
            raise RuntimeError("Darwin did not observe both gated group members")
        _send(target_parent, {"exec": argv})
        result, fds = _receive(target_parent, allow_eof=True)
        for fd in fds:
            os.close(fd)
        if result is not None or fds:
            raise RuntimeError("Registered target could not exec" + (
                ": " + str(result.get("error", {}).get("message", "invalid response")) if result else ""))
        group.record["exec_released"] = True
        return group
    except BaseException:
        # The broker registered each direct Popen immediately and retains it
        # through failure cleanup. Before transfer to a proven group, user exec
        # is still gated. Closing either control cannot release a user target.
        if anchor_parent is not None:
            anchor_parent.close()
        raise
    finally:
        target_parent.close()
        target_child.close()
        if anchor_child is not None:
            anchor_child.close()


class _Broker:
    def __init__(self, request):
        self.receipt, self.row = Path(request["receipt_path"]), request["row"]
        self.address, self.token = request["address"], request["token"]
        self.ipc_directory = Path(self.address).parent
        self.ipc_identity = tuple(request["ipc_directory_identity"])
        if _ipc_directory_identity(self.ipc_directory) != self.ipc_identity:
            raise RuntimeError("Owned broker directory identity changed before startup")
        self.groups, self.pending, self.backend = {}, {}, _DarwinGroups()
        self.row["ownership"] = {"method": "macos-cooperative-broker", "supervisor_pid": os.getpid(),
            "caller_pid": os.getppid(), "session_id": os.getsid(0), "child_launched": False,
            "scope": "trusted complete pytest, installed smoke, and audited registered UI children",
            "untrusted_containment": False, "registered_detach_required": True,
            "sigchld_default": signal.getsignal(signal.SIGCHLD) == signal.SIG_DFL,
            "membership_query": "proc_listpids(PROC_PGRP_ONLY=2, owned_pgid)", "groups": [],
            "ipc": {"directory": str(self.ipc_directory), "endpoint": self.address, "retired": False}}
        if os.getsid(0) != os.getpid() or os.getpgrp() != os.getpid():
            raise RuntimeError("macOS broker is not its dedicated session leader")
        if self.backend.members(os.getpid()) != [os.getpid()]:
            raise RuntimeError("macOS broker initial group is not exclusively owned")

    def save(self):
        self.row["ownership"]["groups"] = [value.record for value in self.groups.values()]
        self.row["ownership"]["pending_bootstraps"] = [
            {"pid": pid, "role": value[1]} for pid, value in self.pending.items()]
        _common._save(self.receipt, self.row)

    def spawn(self, argv, cwd, environment, streams):
        if len(self.groups) >= MAX_GROUPS:
            raise RuntimeError("Registered spawn count exceeds its bound")
        environment = dict(environment)
        environment[SOCKET_ENV], environment[TOKEN_ENV] = self.address, self.token
        group_id = secrets.token_hex(16)
        def on_process(process, role):
            self.pending[process.pid] = (process, role)
            self.row["ownership"]["child_launched"] = True
            self.save()
        def on_group(group):
            self.groups[group_id] = group
            del self.pending[group.child.pid]
            del self.pending[group.anchor.pid]
            self.save()
        group = _launch_group(argv, cwd, environment, streams, self.backend, group_id, on_process, on_group)
        self.save()
        return group

    def request(self, value, fds):
        if (value.get("schema") != WIRE_SCHEMA or not isinstance(value.get("token"), str)
                or not hmac.compare_digest(value["token"], self.token)):
            raise ValueError("Unauthenticated macOS ownership request")
        base = {"schema", "token", "op"}
        if value.get("op") == "spawn":
            if set(value) != base | {"argv", "cwd", "env", "streams"}:
                raise ValueError("Invalid registered spawn request fields")
            argv, environment = _argv(value["argv"]), _environment(value["env"])
            cwd = Path(value["cwd"]).resolve(strict=True)
            if not cwd.is_dir() or set(value["streams"]) != {"stdin", "stdout", "stderr"}:
                raise ValueError("Invalid registered spawn directory or streams")
            streams, used = {}, []
            for name, item in value["streams"].items():
                if item == "null":
                    streams[name] = subprocess.DEVNULL
                elif name == "stderr" and item == "stdout":
                    streams[name] = subprocess.STDOUT
                elif type(item) is int and 0 <= item < len(fds):
                    streams[name] = fds[item]
                    used.append(item)
                else:
                    raise ValueError("Invalid registered stream descriptor")
            if sorted(used) != list(range(len(fds))):
                raise ValueError("Unused or aliased registered stream descriptors")
            group = self.spawn(argv, cwd, environment, streams)
            return {"pid": group.child.pid, "group_id": group.group_id, "returncode": None}
        if value.get("op") not in ("poll", "terminate") or set(value) != base | {"group_id", "pid"} or fds:
            raise ValueError("Unsupported registered ownership operation")
        group = self.groups.get(value["group_id"])
        if group is None or type(value["pid"]) is not int or value["pid"] != group.child.pid:
            raise ValueError("Unknown registered process identity")
        if value["op"] == "poll":
            old = group.record["cleanup_complete"]
            code = group.poll()
            if old != group.record["cleanup_complete"]:
                self.save()
            return code
        group.cleanup()
        self.save()
        return {"method": "registered macOS group drained before anchor reap",
                "launcher_reaped": True, "cleanup_complete": True,
                "exit_code": group.record["exit_code"], "group": group.record}

    def cleanup(self):
        failures = []
        pending_reaped = []
        deadline = time.monotonic() + CLEANUP_SECONDS
        for group in self.groups.values():
            try:
                group.cleanup(deadline)
            except BaseException as error:
                failures.append({"pgid": group.pgid, "type": type(error).__name__, "message": str(error)})
        for pid, (process, role) in list(self.pending.items()):
            try:
                # These are retained direct children, not sampled descendants.
                # No user exec was released before the group-transfer boundary.
                process.kill()
                code = process.wait(timeout=max(.001, deadline - time.monotonic()))
                pending_reaped.append({"pid": pid, "role": role, "exit_code": code})
                del self.pending[pid]
            except BaseException as error:
                failures.append({"bootstrap_pid": pid, "type": type(error).__name__, "message": str(error)})
        self.row["cleanup"] = {"actions": [], "observed": [], "reaped": [], "remaining": [],
                               "group_failures": failures, "pending_bootstraps_reaped": pending_reaped,
                               "membership_scope": "registered pinned groups and retained gated direct children only"}
        for group in self.groups.values():
            for field in ("actions", "observed", "reaped", "remaining"):
                self.row["cleanup"][field].extend(group.record["cleanup"][field])
        self.row["descendants_remained_after_target_completion"] = any(
            group.record["descendants_remained_after_target_completion"] for group in self.groups.values())
        self.row["cleanup"]["remaining"].extend(self.pending)
        self.row["cleanup_complete"] = not failures and not self.pending and all(
            group.record["cleanup_complete"] for group in self.groups.values())
        self.row["ownership"]["groups"] = [group.record for group in self.groups.values()]
        self.row["ownership"]["pending_bootstraps"] = [
            {"pid": pid, "role": value[1]} for pid, value in self.pending.items()]
        if failures:
            raise RuntimeError("One or more registered groups did not complete cleanup")


def _broker_main(request, owner_watch):
    receipt, row = Path(request["receipt_path"]), request["row"]
    broker = main = server = None
    def interrupted(signum, frame):
        raise RuntimeError("macOS broker interrupted by signal " + str(signum))
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    # An inherited SIG_IGN/NOCLDWAIT policy would release an exited anchor
    # before our wait. The dedicated broker owns all of its wait operations.
    signal.signal(signal.SIGCHLD, signal.SIG_DFL)
    try:
        broker = _Broker(request)
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.bind(request["address"])
        server.listen(8)
        main = broker.spawn(row["argv"], row["cwd"], os.environ.copy(),
                            {"stdin": subprocess.DEVNULL, "stdout": None, "stderr": None})
        row.update(state="running", target_pid=main.child.pid)
        broker.save()
        deadline = row["started_monotonic"] + row["timeout_seconds"]
        while True:
            code = main.poll()
            if code is not None:
                row.update(state="exited", target_completed=True, exit_code=code)
                break
            if time.monotonic() >= deadline:
                row["timed_out"] = True
                _common._failure(row, "timed_out", TimeoutError("Owned macOS command exceeded its timeout"))
                break
            readable, _, _ = select.select([server, owner_watch], [], [], POLL_SECONDS)
            if owner_watch in readable and not os.read(owner_watch, 1):
                raise RuntimeError("macOS broker owner pipe closed before terminal cleanup")
            if server in readable:
                channel, _ = server.accept()
                with channel:
                    channel.settimeout(BOOTSTRAP_SECONDS)
                    fds = []
                    try:
                        value, fds = _receive(channel)
                        result = broker.request(value, fds)
                        _send(channel, {"ok": True, "result": result})
                    except BaseException as error:
                        try:
                            _send(channel, {"ok": False, "error": type(error).__name__ + ": " + str(error)})
                        except OSError:
                            pass
                        # A malformed/unsupported registered route cannot turn
                        # into a complete ownership claim for this suite.
                        raise
                    finally:
                        for fd in fds:
                            os.close(fd)
    except BaseException as error:
        _common._failure(row, "ownership_failed" if main is None else "supervisor_failed", error)
    finally:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        try:
            if broker is not None:
                broker.cleanup()
                if main is not None and row["exit_code"] is None:
                    row["exit_code"] = main.record["exit_code"]
            else:
                row["cleanup_complete"] = True  # No target launch was attempted.
        except BaseException as error:
            row["prior_error"] = row.get("error")
            _common._failure(row, "cleanup_failed", error)
        if server is not None:
            server.close()
        if broker is not None:
            if row.get("cleanup_complete") is True:
                try:
                    _retire_ipc(broker.ipc_directory, broker.ipc_identity)
                    row["ownership"]["ipc"]["retired"] = True
                except BaseException as error:
                    row["cleanup_complete"] = False
                    row["unresolved_ipc_directory"] = str(broker.ipc_directory)
                    row["prior_error"] = row.get("error")
                    _common._failure(row, "cleanup_failed", error)
            else:
                row["unresolved_ipc_directory"] = str(broker.ipc_directory)
        os.close(owner_watch)
        _common._finish(receipt, row)


def run_mac_owned(argv, *, cwd, env, timeout, stdout, stderr, receipt_path):
    """Run a trusted cooperative command; refuse non-Darwin before launch."""
    argv, cwd, receipt = _common._validate(argv, cwd, env, timeout, stdout, stderr, receipt_path)
    row = _common._new_row(argv, cwd, timeout)
    raw = Path(__file__).read_bytes()
    row["common_helper"], row["helper"] = row["helper"], {
        "path": str(Path(__file__).resolve()), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
    _common._save(receipt, row, create=True)
    if sys.platform != "darwin":
        row["cleanup_complete"] = True
        row["ownership"] = {"method": "unsupported", "child_launched": False}
        _common._failure(row, "unsupported", RuntimeError("Cooperative macOS ownership requires actual Darwin"))
        _common._finish(receipt, row)
        _common._raise_failure(row)
    directory = directory_identity = supervisor = watch_read = watch_write = None
    try:
        directory = Path(tempfile.mkdtemp(prefix="ijxx-mac-", dir=Path("/tmp").resolve(strict=True)))
        directory.chmod(0o700)
        directory_identity = _ipc_directory_identity(directory)
        address = str(directory / "broker.sock")
        watch_read, watch_write = os.pipe()
        if len(os.fsencode(address)) >= 104:
            raise ValueError("Private macOS broker socket path is too long")
        request = _encode({"receipt_path": str(receipt), "row": row,
                           "address": address, "token": secrets.token_hex(32),
                           "ipc_directory_identity": directory_identity})
        previous = signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGTERM, signal.SIGINT})
        try:
            supervisor = subprocess.Popen([sys.executable, "-I", "-B", str(Path(__file__).resolve()),
                                           "--broker", str(watch_read)], cwd=cwd, env=env,
                                          stdin=subprocess.PIPE, stdout=stdout, stderr=stderr,
                                          close_fds=True, pass_fds=(watch_read,), start_new_session=True)
        finally:
            signal.pthread_sigmask(signal.SIG_SETMASK, previous)
        os.close(watch_read)
        watch_read = None
        supervisor.communicate(request, timeout=float(timeout) + CLEANUP_SECONDS + 10.)
        observed = json.loads(receipt.read_bytes())
        if (observed.get("schema") != SCHEMA or observed.get("argv") != argv
                or observed.get("ownership", {}).get("supervisor_pid") != supervisor.pid
                or supervisor.returncode != 0 or observed.get("state") in ("starting", "running")):
            raise RuntimeError("macOS broker did not produce a valid terminal receipt")
        row = observed
    except BaseException as error:
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
        _common._failure(row, "launch_failed" if supervisor is None else "supervisor_failed", error)
        row["supervisor_exit_code"] = None if supervisor is None else supervisor.poll()
        row["supervisor_pid_if_unresolved"] = (
            supervisor.pid if supervisor is not None and supervisor.poll() is None else None)
        if supervisor is None:
            row["cleanup_complete"] = True
        _common._finish(receipt, row)
    finally:
        if watch_read is not None:
            os.close(watch_read)
        if watch_write is not None:
            os.close(watch_write)
        if directory is not None:
            try:
                if supervisor is None or (supervisor.poll() is not None and row.get("cleanup_complete") is True):
                    # The broker also retires IPC after drain, so owner death
                    # cannot strand it. Missing already-retired nodes are safe.
                    _retire_ipc(directory, directory_identity)
                else:
                    raise RuntimeError("macOS ownership cleanup is unresolved; IPC retained")
            except BaseException as error:
                row["unresolved_ipc_directory"] = str(directory)
                row["cleanup_complete"] = False
                row["prior_error"] = row.get("error")
                _common._failure(row, "cleanup_failed", error)
                _common._finish(receipt, row)
    _common._raise_failure(row)
    return row


if __name__ == "__main__":
    if sys.platform != "darwin":
        raise SystemExit("This helper has no non-Darwin command mode")
    if len(sys.argv) == 3 and sys.argv[1] == "--broker":
        signal.pthread_sigmask(signal.SIG_UNBLOCK, {signal.SIGTERM, signal.SIGINT})
        raw = sys.stdin.buffer.read(MAX_MESSAGE_BYTES + 1)
        if len(raw) > MAX_MESSAGE_BYTES:
            raise SystemExit("macOS broker request exceeds its byte bound")
        _broker_main(_decode(raw), int(sys.argv[2]))
    elif len(sys.argv) == 3 and sys.argv[1] == "--target":
        _child_bootstrap("target", int(sys.argv[2]))
    elif len(sys.argv) == 4 and sys.argv[1] == "--anchor":
        _child_bootstrap("anchor", int(sys.argv[2]), int(sys.argv[3]))
    else:
        raise SystemExit("This helper has no standalone unowned-command mode")
