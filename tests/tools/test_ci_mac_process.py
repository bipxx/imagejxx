"""macOS CI ownership tests with explicitly separate evidence scopes.

Protocol/metadata tests use synthetic records, and POSIX socket tests exercise
only transport. They do not qualify Darwin ownership. DarwinLifecycle requires
the actual Darwin kernel and launches only tiny first-party Python fixtures.
No product imports, network, compiler, dependency installation or unsafe links.
"""
import ctypes
import importlib.util
import io
import json
import os
from pathlib import Path
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / "scripts/ci_mac_process.py"
SPEC = importlib.util.spec_from_file_location("ci_mac_process_under_test", HELPER)
mac = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mac)


class ProtocolMetadata(unittest.TestCase):
    """Synthetic protocol evidence, never a Darwin qualification substitute."""
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx mac protocol ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()

    def options(self, **changes):
        kwargs = dict(cwd=self.root, env={"LITERAL": "not for a receipt"},
                      stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                      stderr=subprocess.STDOUT, start_new_session=True)
        kwargs.update(changes)
        return mac._spawn_options([sys.executable, "-I", "-c", "pass"], kwargs)

    def test_closed_protocol_rejects_duplicate_nonfinite_and_nonobject_values(self):
        for payload in (b'{"op":"poll","op":"spawn"}', b'{"value":NaN}', b'[]'):
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                mac._decode(payload)
        self.assertEqual(mac._decode(mac._encode({"value": "exact \u03bb", "count": 2})),
                         {"value": "exact \u03bb", "count": 2})

    def test_protocol_and_argv_bounds_refuse_without_a_launch(self):
        with self.assertRaises(ValueError):
            mac._encode({"value": "x" * mac.MAX_MESSAGE_BYTES})
        for value in ([], "python", [""], ["bad\0arg"], ["x" * 65536]):
            with self.subTest(value_type=type(value).__name__), self.assertRaises(ValueError):
                mac._argv(value)
        self.assertEqual(mac._argv(["python", "literal space"]), ["python", "literal space"])

    def test_empty_nonfirst_arguments_match_the_shared_owner_contract(self):
        for values in ([sys.executable, "-c", ""], ["python", "", "literal", ""]):
            with self.subTest(values=values):
                expected, _, _ = mac._common._validate(values, self.root, {}, 1,
                    subprocess.DEVNULL, subprocess.DEVNULL, self.root / "not-created.json")
                self.assertEqual(mac._argv(tuple(values)), expected)
        for values in (["", "argument"], ["python"] + [""] * 1024):
            with self.subTest(argument_count=len(values)), self.assertRaises(ValueError):
                mac._argv(values)

    def test_failure_diagnostics_preserve_pending_python310_errors_before_cleanup(self):
        receipt = self.root / "owned.json"
        receipt.write_text('{"scope":"synthetic cleanup journal"}', encoding="utf-8")
        (self.root / "command.log").write_text("literal failure log", encoding="utf-8")
        fixture = SimpleNamespace(root=self.root, receipt=receipt)
        error = (AssertionError, AssertionError("literal failure"), None)
        for mode in ("pending_python310", "reported_failure", "reported_error", "no_failure"):
            with self.subTest(mode=mode):
                result = SimpleNamespace(failures=[], errors=[])
                pending = [(fixture, None), (object(), error)]
                if mode == "pending_python310":
                    pending.append((fixture, error))
                elif mode == "reported_failure":
                    result.failures.append((fixture, "literal traceback"))
                elif mode == "reported_error":
                    result.errors.append((fixture, "literal traceback"))
                fixture._outcome = SimpleNamespace(result=result, errors=pending)
                with patch.object(sys, "stderr", new_callable=io.StringIO) as emitted:
                    DarwinLifecycle.failure_diagnostics(fixture)
                if mode == "no_failure":
                    self.assertEqual(emitted.getvalue(), "")
                else:
                    self.assertEqual(emitted.getvalue(),
                        'DARWIN FAILURE DIAGNOSTIC owned.json:\n{"scope":"synthetic cleanup journal"}\n'
                        'DARWIN FAILURE DIAGNOSTIC command.log:\nliteral failure log\n')
                self.assertTrue(receipt.is_file())
                self.assertEqual((self.root / "command.log").read_text(), "literal failure log")

    def test_only_audited_popen_options_and_real_streams_are_supported(self):
        request, fds = self.options()
        self.assertEqual(request["streams"], {"stdin": "null", "stdout": "null", "stderr": "stdout"})
        self.assertEqual(fds, [])
        for changes in ({"shell": True}, {"preexec_fn": lambda: None}, {"pass_fds": ()},
                        {"start_new_session": False}, {"close_fds": False}, {"stdout": subprocess.PIPE},
                        {"env": {"bad=key": "value"}}):
            with self.subTest(keys=list(changes)), self.assertRaises(ValueError):
                self.options(**changes)

    def test_real_open_stream_descriptor_is_passed_without_reopening_path(self):
        with (self.root / "producer.log").open("wb") as stream:
            request, fds = self.options(stdout=stream)
            self.assertEqual(fds, [stream.fileno()])
            self.assertEqual(request["streams"], {"stdin": "null", "stdout": 0, "stderr": "stdout"})
            self.assertFalse(stream.closed)

    def test_unregistered_or_unsupported_spawn_never_falls_back_to_popen(self):
        for platform in ("linux", "darwin"):
            with self.subTest(platform=platform), patch.object(mac, "sys", SimpleNamespace(platform=platform)), \
                    patch.dict(os.environ, {}, clear=True), patch.object(mac.subprocess, "Popen") as popen:
                with self.assertRaises(RuntimeError):
                    mac.spawn_registered(["unused"], start_new_session=True)
                popen.assert_not_called()

    def test_required_shared_wrapper_refuses_missing_both_credentials_without_popen(self):
        path = ROOT / "tests/python/settings_wand_ui_runner.py"
        spec = importlib.util.spec_from_file_location("mac_required_shared_wrapper", path)
        shared = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(shared)
        with patch.object(sys, "platform", "darwin"), \
                patch.dict(os.environ, {"IMAGEJXX_REQUIRE_MAC_PROCESS_OWNER": "1"}, clear=True), \
                patch.object(subprocess, "Popen") as popen:
            with self.assertRaises(RuntimeError):
                shared.spawn_python_ui_child(["unused"], start_new_session=True)
            popen.assert_not_called()

    def test_ipc_retirement_requires_original_directory_and_only_expected_socket_kind(self):
        directory = self.root / "owned-ipc"
        directory.mkdir()
        identity = mac._ipc_directory_identity(directory)
        with self.assertRaisesRegex(RuntimeError, "identity changed"):
            mac._retire_ipc(directory, (identity[0], identity[1] + 1))
        self.assertTrue(directory.is_dir())
        unexpected = directory / "broker.sock"
        unexpected.write_bytes(b"not a socket")
        with self.assertRaisesRegex(RuntimeError, "replaced"):
            mac._retire_ipc(directory, identity)
        self.assertEqual(unexpected.read_bytes(), b"not a socket")
        unexpected.unlink()
        extra = directory / "unexpected.txt"
        extra.write_bytes(b"must remain")
        with self.assertRaises(OSError):
            mac._retire_ipc(directory, identity)
        self.assertEqual(extra.read_bytes(), b"must remain")
        extra.unlink()
        mac._retire_ipc(directory, identity)
        self.assertFalse(directory.exists())
        mac._retire_ipc(directory, identity)  # Parent tolerates broker retirement.

    def test_non_darwin_run_has_durable_unsupported_receipt_and_no_child(self):
        receipt = self.root / "unsupported.json"
        with patch.object(mac, "sys", SimpleNamespace(platform="linux")), \
                patch.object(mac.subprocess, "Popen") as popen:
            with self.assertRaises(mac.OwnedProcessUnsupported) as caught:
                mac.run_mac_owned([sys.executable, "-I", "-c", "pass"], cwd=self.root, env={}, timeout=1,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, receipt_path=receipt)
            popen.assert_not_called()
        self.assertEqual(json.loads(receipt.read_bytes()), caught.exception.receipt)
        self.assertEqual(caught.exception.receipt["state"], "unsupported")
        self.assertTrue(caught.exception.receipt["cleanup_complete"])
        self.assertFalse(caught.exception.receipt["ownership"]["child_launched"])

    def test_broker_authentication_and_group_capability_are_required(self):
        broker = object.__new__(mac._Broker)
        broker.token, broker.groups = "a" * 64, {}
        base = {"schema": mac.WIRE_SCHEMA, "token": broker.token, "op": "poll", "group_id": "b" * 32, "pid": 123}
        for row in ({**base, "token": "c" * 64}, base, {**base, "op": "kill_pid"}, {**base, "extra": True}):
            with self.subTest(operation=row["op"]), self.assertRaises(ValueError):
                broker.request(row, [])

    def test_direct_popen_is_registered_before_interrupt_mask_is_restored(self):
        events = []
        def masked(operation, value):
            events.append(("mask", operation, value))
            return {99}
        fake_signal = SimpleNamespace(pthread_sigmask=masked, SIG_BLOCK=1, SIG_SETMASK=2, SIGTERM=15, SIGINT=2)
        process = SimpleNamespace(pid=301)
        def launched(*args, **kwargs):
            events.append(("popen", args, kwargs))
            return process
        def registered(value, role):
            events.append(("registered", value.pid, role))
        with patch.object(mac, "signal", fake_signal), patch.object(mac.subprocess, "Popen", side_effect=launched):
            self.assertIs(mac._registered_popen(registered, "gated_target", ["fixture"]), process)
        self.assertEqual([event[0] for event in events], ["mask", "popen", "registered", "mask"])
        self.assertEqual(events[-1], ("mask", 2, {99}))

    def test_proxy_preserves_actual_pid_wait_timeout_and_explicit_tree_cleanup(self):
        proxy = mac.RegisteredProcess("unused", "t" * 64,
                                      {"pid": 211, "group_id": "a" * 32, "returncode": None}, ["python"])
        with patch.object(mac, "_rpc", return_value=None) as request:
            with self.assertRaises(subprocess.TimeoutExpired):
                proxy.wait(timeout=0)
            self.assertEqual(request.call_args.args[2], {"op": "poll", "group_id": "a" * 32, "pid": 211})
        cleanup = {"cleanup_complete": True, "launcher_reaped": True, "exit_code": -9}
        with patch.object(mac, "_rpc", return_value=cleanup) as request:
            self.assertEqual(proxy.terminate_tree(), cleanup)
            self.assertEqual(proxy.poll(), -9)
            self.assertEqual(proxy.wait(timeout=0), -9)
            self.assertEqual(proxy.terminate_tree(), cleanup)
            self.assertEqual(request.call_count, 1)
            self.assertEqual(request.call_args.args[2]["op"], "terminate")
        self.assertEqual(proxy.pid, 211)

    def test_group_cleanup_keeps_anchor_until_observed_drain_and_never_kills_sampled_pids(self):
        events = []
        states = iter(([101, 102, 103], [101, 102, 103], [102, 103], [102, 103], [102]))
        def members(pgid):
            self.assertEqual(pgid, 101)
            values = next(states)
            events.append(("members", values))
            return values
        def reaped(timeout):
            events.append(("anchor_reaped", timeout))
            self.assertEqual(events[-2], ("members", [102]))
            return -9
        child = SimpleNamespace(pid=101, poll=Mock(return_value=-9))
        anchor = SimpleNamespace(pid=102, wait=Mock(side_effect=reaped))
        channel = Mock()
        group = mac._Group(child, anchor, channel, SimpleNamespace(members=members), ["fixture"], "g" * 32)
        killpg, kill = Mock(), Mock()
        with patch.object(mac, "os", SimpleNamespace(killpg=killpg, kill=kill)), \
                patch.object(mac, "signal", SimpleNamespace(SIGKILL=9)), patch.object(mac.time, "sleep"):
            group.cleanup()
        self.assertTrue(group.record["cleanup_complete"])
        self.assertEqual(group.record["cleanup"]["remaining"], [])
        self.assertEqual(group.record["cleanup"]["observed"], [101, 102, 103])
        self.assertEqual([item.args for item in killpg.call_args_list], [(101, 9)] * 2)
        kill.assert_not_called()
        anchor.wait.assert_called_once()
        channel.close.assert_called_once()
        self.assertEqual(group.record["cleanup"]["terminal_wait"], "group_drained_before_anchor_reap")

    def test_no_signalable_member_error_requires_observed_drain_not_assumed_success(self):
        child = SimpleNamespace(pid=101, poll=Mock(return_value=-9))
        anchor = SimpleNamespace(pid=102, wait=Mock(return_value=-9))
        backend = SimpleNamespace(members=Mock(side_effect=[[102], [102], [102]]))
        group = mac._Group(child, anchor, Mock(), backend, ["fixture"], "g" * 32)
        killpg = Mock(side_effect=PermissionError(1, "no signalable process"))
        with patch.object(mac, "os", SimpleNamespace(killpg=killpg)), \
                patch.object(mac, "signal", SimpleNamespace(SIGKILL=9)):
            group.cleanup()
        action = group.record["cleanup"]["actions"][0]
        self.assertEqual(action["successful_sends"], 0)
        self.assertEqual(action["last_error"], {"type": "PermissionError", "errno": 1})
        self.assertTrue(group.record["cleanup_complete"])
        self.assertEqual(group.record["cleanup"]["remaining"], [])

    def test_failed_drain_never_reaps_anchor_or_claims_complete(self):
        child = SimpleNamespace(pid=101, poll=Mock(return_value=-9))
        anchor = SimpleNamespace(pid=102, wait=Mock(return_value=-9))
        backend = SimpleNamespace(members=Mock(return_value=[102, 103]))
        group = mac._Group(child, anchor, Mock(), backend, ["fixture"], "g" * 32)
        with patch.object(mac, "os", SimpleNamespace(killpg=Mock())), \
                patch.object(mac, "signal", SimpleNamespace(SIGKILL=9)), self.assertRaises(TimeoutError):
            group.cleanup(deadline=time.monotonic() - 1)
        anchor.wait.assert_not_called()
        self.assertFalse(group.record["cleanup_complete"])
        self.assertEqual(group.record["cleanup"]["remaining"], [103])

    def test_darwin_abi_metadata_uses_only_the_supplied_group_and_refuses_truncation(self):
        calls = []
        def observed(kind, pgid, buffer, size):
            calls.append((kind, pgid, size))
            buffer[0], buffer[1] = 202, 203
            return 2 * ctypes.sizeof(ctypes.c_int)
        function = Mock(side_effect=observed)
        with patch.object(mac, "sys", SimpleNamespace(platform="darwin")), \
                patch.object(mac.ctypes, "CDLL", return_value=SimpleNamespace(proc_listpids=function)):
            backend = mac._DarwinGroups()
        self.assertEqual(backend.members(202), [202, 203])
        self.assertEqual(calls, [(2, 202, (mac.MAX_PROCESSES + 1) * ctypes.sizeof(ctypes.c_int))])
        function.side_effect = lambda kind, pgid, buffer, size: size
        with self.assertRaisesRegex(RuntimeError, "bound"):
            backend.members(202)
        with self.assertRaises(ValueError):
            backend.members(0)


@unittest.skipUnless(hasattr(socket, "SCM_RIGHTS") and hasattr(socket.socket, "sendmsg"),
                     "POSIX transport only; not native Darwin ownership evidence")
class PosixSocketTransport(unittest.TestCase):
    def pair(self):
        left, right = socket.socketpair()
        left.settimeout(1)
        right.settimeout(1)
        self.addCleanup(left.close)
        self.addCleanup(right.close)
        return left, right

    def test_exact_payload_and_real_descriptor_transfer(self):
        left, right = self.pair()
        with tempfile.TemporaryFile(mode="w+b") as stream:
            stream.write(b"literal stream\0\xff")
            stream.seek(0)
            mac._send(left, {"op": "fixture", "literal": "\u03bb"}, [stream.fileno()])
            value, fds = mac._receive(right)
            self.assertEqual(value, {"op": "fixture", "literal": "\u03bb"})
            self.assertEqual(len(fds), 1)
            with os.fdopen(fds[0], "rb") as received:
                self.assertEqual(received.read(), b"literal stream\0\xff")
            self.assertFalse(stream.closed)

    def test_oversize_declared_frame_refuses_before_payload_read(self):
        left, right = self.pair()
        left.sendall(struct.pack("!I", mac.MAX_MESSAGE_BYTES + 1))
        with self.assertRaisesRegex(ValueError, "bound"):
            mac._receive(right)

    def test_closed_owned_unix_socket_is_retired_without_recursive_cleanup(self):
        with tempfile.TemporaryDirectory(prefix="ijxx-ipc-", dir=Path("/tmp").resolve(strict=True)) as root:
            directory = Path(root) / "owned"
            directory.mkdir()
            identity = mac._ipc_directory_identity(directory)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
                server.bind(str(directory / "broker.sock"))
            mac._retire_ipc(directory, identity)
            self.assertFalse(directory.exists())

    def test_clean_exec_eof_is_distinct_from_partial_protocol(self):
        left, right = self.pair()
        left.close()
        self.assertEqual(mac._receive(right, allow_eof=True), (None, []))
        left, right = self.pair()
        left.sendall(b"\0\0")
        left.close()
        with self.assertRaises(ConnectionError):
            mac._receive(right, allow_eof=True)


# Keep unresolved native fixture witnesses alive through the failing tool run;
# their scratch is retained rather than retired while a lifetime is unresolved.
_UNRESOLVED_DARWIN_FIXTURES = []


class DarwinExitWitness:
    """An independent kernel NOTE_EXIT attached before external acknowledgment."""
    def __init__(self, pid):
        if sys.platform != "darwin":
            raise RuntimeError("A native Darwin kernel witness is required")
        self.pid, self.event = pid, None
        self.queue = select.kqueue()
        try:
            event = select.kevent(pid, filter=select.KQ_FILTER_PROC,
                flags=select.KQ_EV_ADD | select.KQ_EV_ENABLE | select.KQ_EV_ONESHOT,
                fflags=select.KQ_NOTE_EXIT)
            self._observe(self.queue.control([event], 1, 0))
            if self.event is not None:
                raise AssertionError("Process exited before its live acknowledgment")
        except BaseException:
            self.queue.close()
            raise

    def _observe(self, events):
        for event in events:
            if event.flags & select.KQ_EV_ERROR:
                raise OSError(event.data, "Native process witness registration failed")
            if (event.ident != self.pid or event.filter != select.KQ_FILTER_PROC
                    or not event.fflags & select.KQ_NOTE_EXIT):
                raise AssertionError("Unexpected native process witness event")
            self.event = {"pid": event.ident, "filter": event.filter, "flags": event.flags,
                          "fflags": event.fflags, "data": event.data}

    def exited(self, timeout=0):
        if self.event is None:
            self._observe(self.queue.control(None, 1, timeout))
        return self.event is not None

    def close(self):
        self.queue.close()


@unittest.skipUnless(sys.platform == "darwin", "MANDATORY native Darwin lifecycle qualification; not replaced by transport tests")
class DarwinLifecycle(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx mac lifecycle ")
        self.temporary, self.independent_cleanup_complete = temporary, True
        self.addCleanup(self.retire_temporary)
        self.root = Path(temporary.name).resolve()
        self.receipt = self.root / "owned.json"
        self.addCleanup(self.failure_diagnostics)

    def retire_temporary(self):
        if self.independent_cleanup_complete:
            self.temporary.cleanup()
        else:
            self.temporary._finalizer.detach()
            print("DARWIN UNRESOLVED FIXTURE RETAINED: " + str(self.root), file=sys.stderr)

    def failure_diagnostics(self):
        outcome = self._outcome
        result = getattr(outcome, "result", None)
        recorded = list(getattr(result, "failures", ())) + list(getattr(result, "errors", ()))
        # Python 3.10 feeds pending outcome errors to TestResult only after
        # doCleanups; newer versions have already populated the result lists.
        pending = getattr(outcome, "errors", ())
        if not any(test is self and error is not None for test, error in recorded + list(pending)):
            return
        # Native fixtures contain at most two tiny groups. Preserve their full
        # terminal receipt in the test log before owned temporary-file cleanup.
        paths = [(self.receipt, 256 * 1024), (self.root / "command.log", 64 * 1024)]
        paths.extend((self.root / name, 64 * 1024) for name in ("owner.log", "independent-witness.json")
                     if (self.root / name).is_file())
        for path, limit in paths:
            try:
                size = path.stat().st_size
                if size > limit:
                    print(f"DARWIN FAILURE DIAGNOSTIC OVER BOUND: {path.name} bytes={size} limit={limit}", file=sys.stderr)
                else:
                    print(f"DARWIN FAILURE DIAGNOSTIC {path.name}:\n" + path.read_bytes().decode("utf-8", errors="replace"),
                          file=sys.stderr)
            except OSError as error:
                print(f"DARWIN FAILURE DIAGNOSTIC UNAVAILABLE {path.name}: {type(error).__name__}", file=sys.stderr)

    def wait_json(self, path, predicate=lambda value: True, *, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                raw = path.read_bytes()
                self.assertLessEqual(len(raw), 256 * 1024)
                value = json.loads(raw)
                if predicate(value):
                    return value
            except (FileNotFoundError, json.JSONDecodeError):
                pass
            time.sleep(.01)
        self.fail("Native fixture acknowledgment was not completed: " + path.name)

    def independent_fixture(self):
        state = {"owner": None, "control": None, "log": None, "witnesses": {}, "observations": {}}
        self.independent_cleanup_complete = False
        self.addCleanup(self.cleanup_independent_fixture, state)
        witness_source = (
            "import json,os,sys,time\nfrom pathlib import Path\n"
            "root=Path(sys.argv[1]); role=sys.argv[2]\n"
            "identity={'pid':os.getpid(),'parent_pid':os.getppid(),'pgid':os.getpgrp(),'role':role}\n"
            "(root/(role+'.json')).write_text(json.dumps(identity))\n"
            "deadline=time.monotonic()+45\n"
            "while not (root/(role+'.ack')).exists():\n"
            " if time.monotonic()>deadline: raise SystemExit(91)\n"
            " time.sleep(.005)\n"
            "(root/(role+'-ack.json')).write_text(json.dumps(identity))\n"
            "while time.monotonic()<deadline: time.sleep(.02)\n"
            "raise SystemExit(92)\n")
        target_source = (
            "import json,os,subprocess,sys,time\nfrom pathlib import Path\n"
            "root=Path(sys.argv[1])\n"
            "leaf=subprocess.Popen([sys.executable,'-I','-B',str(root/'witness.py'),str(root),'leaf'])\n"
            "deadline=time.monotonic()+35\n"
            "while not (root/'leaf-ack.json').exists():\n"
            " if time.monotonic()>deadline: raise SystemExit(93)\n"
            " time.sleep(.005)\n"
            "if leaf.poll() is not None: raise SystemExit(94)\n"
            "(root/'target-live.json').write_text(json.dumps({'pid':os.getpid(),'parent_pid':os.getppid(),'leaf':leaf.pid}))\n"
            "while not (root/'release-target').exists():\n"
            " if time.monotonic()>deadline: raise SystemExit(95)\n"
            " time.sleep(.005)\n")
        owner_source = (
            "import importlib.util,json,os,subprocess,sys\nfrom pathlib import Path\n"
            "root=Path(sys.argv[1])\n"
            "(root/'owner.json').write_text(json.dumps({'pid':os.getpid(),'parent_pid':os.getppid()}))\n"
            "spec=importlib.util.spec_from_file_location('independent_mac_owner',sys.argv[2])\n"
            "mac=importlib.util.module_from_spec(spec);spec.loader.exec_module(mac)\n"
            "with (root/'command.log').open('xb') as log:\n"
            " mac.run_mac_owned([sys.executable,'-I','-B',str(root/'target.py'),str(root)],cwd=root,env=os.environ.copy(),timeout=30,stdout=log,stderr=subprocess.STDOUT,receipt_path=root/'owned.json')\n")
        for name, source in (("witness.py", witness_source), ("target.py", target_source), ("owner.py", owner_source)):
            (self.root / name).write_text(source, encoding="utf-8")
        executable = str(Path(sys._base_executable).resolve(strict=True))
        state["control"] = subprocess.Popen([executable, "-I", "-B", str(self.root / "witness.py"), str(self.root), "control"],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        control = self.wait_json(self.root / "control.json")
        state["witnesses"]["control"] = DarwinExitWitness(control["pid"])
        self.assertEqual(control["pid"], state["control"].pid)
        (self.root / "control.ack").write_text("kernel witness installed", encoding="utf-8")
        self.assertEqual(self.wait_json(self.root / "control-ack.json"), control)
        state["observations"]["control_identity"] = control
        state["log"] = (self.root / "owner.log").open("xb")
        state["owner"] = subprocess.Popen([executable, "-I", "-B", str(self.root / "owner.py"), str(self.root), str(HELPER)],
            stdin=subprocess.DEVNULL, stdout=state["log"], stderr=subprocess.STDOUT, start_new_session=True)
        owner = self.wait_json(self.root / "owner.json")
        self.assertEqual(owner["pid"], state["owner"].pid)
        running = self.wait_json(self.receipt, lambda row: row.get("state") == "running")
        group = running["ownership"]["groups"][0]
        for role, pid in (("broker", running["ownership"]["supervisor_pid"]),
                          ("target", group["pid"]), ("anchor", group["anchor_pid"])):
            state["witnesses"][role] = DarwinExitWitness(pid)
        self.assertEqual(running["ownership"]["caller_pid"], owner["pid"])
        self.assertEqual(running["target_pid"], group["pid"])
        leaf = self.wait_json(self.root / "leaf.json")
        state["witnesses"]["leaf"] = DarwinExitWitness(leaf["pid"])
        self.assertEqual((leaf["parent_pid"], leaf["pgid"]), (group["pid"], group["pid"]))
        (self.root / "leaf.ack").write_text("kernel witness installed", encoding="utf-8")
        self.assertEqual(self.wait_json(self.root / "leaf-ack.json"), leaf)
        target = self.wait_json(self.root / "target-live.json")
        self.assertEqual(target, {"pid": group["pid"], "parent_pid": running["ownership"]["supervisor_pid"], "leaf": leaf["pid"]})
        self.assertFalse(state["witnesses"]["leaf"].exited())
        state["observations"].update(owner_identity=owner, leaf_identity=leaf, target_identity=target,
            running_ownership=running["ownership"], live_before_action=list(state["witnesses"]))
        self.assert_independent_control_live(state)
        return state

    def assert_independent_control_live(self, state):
        self.assertIsNone(state["control"].poll(), "Unrelated acknowledged control was terminated")
        self.assertFalse(state["witnesses"]["control"].exited(), "Kernel observed unrelated control exit")
        state["observations"]["control_live_checks"] = state["observations"].get("control_live_checks", 0) + 1

    def cleanup_independent_fixture(self, state):
        failures = []
        owner = state["owner"]
        if owner is not None:
            try:
                if owner.poll() is None:
                    owner.kill()  # Retained direct child; closes its broker watch pipe.
                owner.wait(timeout=5)
            except BaseException as error:
                failures.append("owner: " + str(error))
        deadline = time.monotonic() + 12
        required = ("leaf", "target", "anchor", "broker") if owner is not None else ()
        for role in required:
            witness = state["witnesses"].get(role)
            if witness is None:
                failures.append(role + ": no independent lifetime was registered")
            else:
                try:
                    if not witness.exited(max(0, deadline - time.monotonic())):
                        failures.append(role + ": no kernel exit event")
                except BaseException as error:
                    failures.append(role + ": " + str(error))
        control = state["control"]
        if control is not None:
            try:
                if control.poll() is None:
                    control.kill()
                control.wait(timeout=5)
                witness = state["witnesses"].get("control")
                if witness is not None and not witness.exited(1):
                    failures.append("control: no kernel exit after direct-child cleanup")
            except BaseException as error:
                failures.append("control: " + str(error))
        if state["log"] is not None:
            state["log"].close()
        state["observations"].update(cleanup_failures=failures,
            exit_events={role: witness.event for role, witness in state["witnesses"].items()})
        (self.root / "independent-witness.json").write_text(
            json.dumps(state["observations"], indent=2) + "\n", encoding="utf-8")
        self.independent_cleanup_complete = not failures
        if failures:
            _UNRESOLVED_DARWIN_FIXTURES.append(state)
            self.fail("Native fixture cleanup unresolved: " + "; ".join(failures))
        for witness in state["witnesses"].values():
            witness.close()

    def assert_independent_owned_exit(self, state):
        deadline = time.monotonic() + 12
        for role in ("leaf", "target", "anchor", "broker"):
            self.assertTrue(state["witnesses"][role].exited(max(0, deadline - time.monotonic())),
                            "Kernel did not observe owned " + role + " exit")
        row = self.wait_json(self.receipt, lambda value: value.get("state") not in ("starting", "running"))
        self.assert_complete(row)
        self.assertIn(state["observations"]["leaf_identity"]["pid"], row["cleanup"]["observed"])
        ipc = row["ownership"]["ipc"]
        self.assertTrue(ipc["retired"])
        self.assertEqual(ipc["endpoint"], str(Path(ipc["directory"]) / "broker.sock"))
        for value in (ipc["endpoint"], ipc["directory"]):
            with self.assertRaises(FileNotFoundError):
                Path(value).lstat()
        self.assert_independent_control_live(state)
        return row

    def run_fixture(self, source, *, timeout=8, extra_env=None):
        script = self.root / "target.py"
        script.write_text(source, encoding="utf-8")
        environment = os.environ.copy()
        environment.update(extra_env or {})
        with (self.root / "command.log").open("xb") as log:
            return mac.run_mac_owned([sys.executable, "-I", "-B", str(script), str(self.root), str(HELPER)],
                cwd=self.root, env=environment, timeout=timeout, stdout=log, stderr=subprocess.STDOUT,
                receipt_path=self.receipt)

    def assert_complete(self, row):
        self.assertLessEqual(self.receipt.stat().st_size, 256 * 1024)
        self.assertEqual(json.loads(self.receipt.read_bytes()), row)
        self.assertTrue(row["cleanup_complete"])
        self.assertEqual(row["ownership"]["method"], "macos-cooperative-broker")
        self.assertEqual(row["ownership"]["session_id"], row["ownership"]["supervisor_pid"])
        self.assertIs(row["ownership"]["untrusted_containment"], False)
        self.assertIs(row["ownership"]["sigchld_default"], True)
        self.assertEqual(row["cleanup"]["remaining"], [])
        self.assertEqual(row["ownership"]["pending_bootstraps"], [])
        for group in row["ownership"]["groups"]:
            self.assertTrue(group["cleanup_complete"])
            self.assertEqual(group["cleanup"]["terminal_wait"], "group_drained_before_anchor_reap")
            self.assertNotEqual(group["pid"], group["anchor_pid"])

    def test_actual_exit_and_nonzero_status_are_returned_without_environment_values(self):
        row = self.run_fixture("import sys\nprint('literal output')\nsys.exit(7)\n",
                               extra_env={"MAC_PRIVATE_TEST_VALUE": "do-not-retain-environment-76543"})
        self.assert_complete(row)
        self.assertEqual((row["state"], row["exit_code"]), ("exited", 7))
        self.assertFalse(row["descendants_remained_after_target_completion"])
        self.assertNotIn("do-not-retain-environment-76543", self.receipt.read_text())
        self.assertEqual((self.root / "command.log").read_text().strip(), "literal output")

    def test_timeout_drains_actual_inherited_grandchild_group(self):
        source = ("import json,os,subprocess,sys,time\nfrom pathlib import Path\n"
                  "leaf=subprocess.Popen([sys.executable,'-I','-c','import time;time.sleep(30)'])\n"
                  "Path(sys.argv[1],'tree.json').write_text(json.dumps({'pid':os.getpid(),'leaf':leaf.pid}))\n"
                  "time.sleep(30)\n")
        with self.assertRaises(mac.OwnedProcessTimeout) as caught:
            self.run_fixture(source, timeout=3)
        row = caught.exception.receipt
        self.assert_complete(row)
        observed = json.loads((self.root / "tree.json").read_text())
        self.assertEqual(row["target_pid"], observed["pid"])
        self.assertIn(observed["leaf"], row["cleanup"]["observed"])
        self.assertTrue(row["timed_out"])

    def test_target_exit_with_remaining_descendant_is_reported_and_cleaned(self):
        source = ("import json,os,subprocess,sys\nfrom pathlib import Path\n"
                  "leaf=subprocess.Popen([sys.executable,'-I','-c','import time;time.sleep(30)'])\n"
                  "Path(sys.argv[1],'tree.json').write_text(json.dumps({'pid':os.getpid(),'leaf':leaf.pid}))\n")
        row = self.run_fixture(source)
        self.assert_complete(row)
        observed = json.loads((self.root / "tree.json").read_text())
        self.assertEqual(row["exit_code"], 0)
        self.assertTrue(row["descendants_remained_after_target_completion"])
        self.assertIn(observed["leaf"], row["cleanup"]["observed"])

    def test_registered_child_keeps_actual_pid_stream_and_explicit_timeout_cleanup(self):
        child = ("import json,os,subprocess,sys,time\nfrom pathlib import Path\n"
                 "leaf=subprocess.Popen([sys.executable,'-I','-c','import time;time.sleep(30)'])\n"
                 "print('registered stdout',flush=True)\n"
                 "Path(sys.argv[1],'child.json').write_text(json.dumps({'pid':os.getpid(),'parent':os.getppid(),'leaf':leaf.pid}))\n"
                 "Path(sys.argv[1],'child.ready').write_text('ready')\ntime.sleep(30)\n")
        (self.root / "child.py").write_text(child, encoding="utf-8")
        source = ("import importlib.util,json,os,subprocess,sys,time\nfrom pathlib import Path\n"
                  "spec=importlib.util.spec_from_file_location('fixture_mac',sys.argv[2])\n"
                  "mac=importlib.util.module_from_spec(spec);spec.loader.exec_module(mac)\n"
                  "root=Path(sys.argv[1])\n"
                  "with (root/'child.log').open('wb') as log:\n"
                  " child=mac.spawn_registered([sys.executable,'-I',str(root/'child.py'),str(root)],cwd=root,env=os.environ.copy(),stdout=log,stderr=subprocess.STDOUT,start_new_session=True)\n"
                  " deadline=time.monotonic()+5\n"
                  " while not (root/'child.ready').exists() and time.monotonic()<deadline:\n"
                  "  assert child.poll() is None;time.sleep(.01)\n"
                  " assert (root/'child.ready').exists()\n"
                  " try: child.wait(timeout=.05)\n"
                  " except subprocess.TimeoutExpired: pass\n"
                  " else: raise RuntimeError('fixture child unexpectedly completed')\n"
                  " cleanup=child.terminate_tree()\n"
                  " assert child.wait(timeout=0)==child.returncode\n"
                  " (root/'proxy.json').write_text(json.dumps({'pid':child.pid,'cleanup':cleanup}))\n")
        row = self.run_fixture(source, timeout=10)
        self.assert_complete(row)
        observed = json.loads((self.root / "child.json").read_text())
        proxy = json.loads((self.root / "proxy.json").read_text())
        self.assertEqual(proxy["pid"], observed["pid"])
        self.assertEqual(observed["parent"], row["ownership"]["supervisor_pid"])
        self.assertEqual(len(row["ownership"]["groups"]), 2)
        self.assertTrue(proxy["cleanup"]["launcher_reaped"])
        self.assertIn(observed["leaf"], proxy["cleanup"]["group"]["cleanup"]["observed"])
        self.assertEqual((self.root / "child.log").read_text().strip(), "registered stdout")

    def test_acknowledged_leaf_kernel_exit_preserves_unrelated_live_control(self):
        state = self.independent_fixture()
        (self.root / "release-target").write_text("allow acknowledged target exit", encoding="utf-8")
        self.assertEqual(state["owner"].wait(timeout=15), 0)
        row = self.assert_independent_owned_exit(state)
        self.assertEqual((row["state"], row["exit_code"]), ("exited", 0))
        self.assertTrue(row["descendants_remained_after_target_completion"])

    def test_owner_death_watch_drains_acknowledged_leaf_with_independent_kernel_witness(self):
        state = self.independent_fixture()
        self.assertIsNone(state["owner"].poll())
        state["owner"].kill()  # Kill only our retained direct owner, not a sampled descendant PID.
        self.assertEqual(state["owner"].wait(timeout=5), -signal.SIGKILL)
        row = self.assert_independent_owned_exit(state)
        self.assertEqual(row["state"], "supervisor_failed")
        self.assertFalse(row["timed_out"])
        self.assertIn("owner pipe closed", row["error"]["message"])

    def test_missing_registration_refuses_without_an_extra_child(self):
        source = ("import importlib.util,os,sys\n"
                  "spec=importlib.util.spec_from_file_location('fixture_mac',sys.argv[2])\n"
                  "mac=importlib.util.module_from_spec(spec);spec.loader.exec_module(mac)\n"
                  "os.environ.pop(mac.SOCKET_ENV);os.environ.pop(mac.TOKEN_ENV)\n"
                  "try: mac.spawn_registered([sys.executable,'-I','-c','pass'],start_new_session=True)\n"
                  "except RuntimeError: pass\nelse: raise RuntimeError('unregistered launch accepted')\n")
        row = self.run_fixture(source)
        self.assert_complete(row)
        self.assertEqual(row["exit_code"], 0)
        self.assertEqual(len(row["ownership"]["groups"]), 1)

    def test_unauthenticated_request_fails_whole_claim_and_cleans_owned_target(self):
        source = ("import importlib.util,os,sys,time\n"
                  "spec=importlib.util.spec_from_file_location('fixture_mac',sys.argv[2])\n"
                  "mac=importlib.util.module_from_spec(spec);spec.loader.exec_module(mac)\n"
                  "mac._rpc(os.environ[mac.SOCKET_ENV],'0'*64,{'op':'poll','group_id':'1'*32,'pid':123})\n"
                  "time.sleep(30)\n")
        with self.assertRaises(mac.OwnedProcessError) as caught:
            self.run_fixture(source)
        row = caught.exception.receipt
        self.assert_complete(row)
        self.assertEqual(row["state"], "supervisor_failed")
        self.assertIn("Unauthenticated", row["error"]["message"])


if __name__ == "__main__":
    if "--require-darwin" in sys.argv[1:]:
        if sys.argv[1:] != ["--require-darwin"]:
            raise SystemExit("The required native selector accepts only --require-darwin")
        if sys.platform != "darwin":
            raise SystemExit("Required Darwin lifecycle qualification cannot run on this platform")
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(DarwinLifecycle)
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        raise SystemExit(0 if result.wasSuccessful() and result.testsRun == 8 and not result.skipped else 1)
    unittest.main()
