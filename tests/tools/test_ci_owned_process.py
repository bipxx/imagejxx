"""Real bounded process-lifecycle tests; no product/dependency build is run.

Each applicable test keeps an unrelated acknowledged control process alive.
Child witnesses are acquired before an acknowledgment lets the target exit:
Windows retains a real process handle; Linux retains a pidfd where available
and independently records /proc start time. The receipt is not the liveness
oracle. All fixtures and output belong to one fresh temporary directory.
"""
from __future__ import annotations

import ctypes
import importlib.util
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / "scripts/ci_owned_process.py"
SPEC = importlib.util.spec_from_file_location("ci_owned_process_under_test", HELPER)
owned = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(owned)
SUPPORTED = sys.platform == "win32" or sys.platform.startswith("linux")
SECRET = "owned-process-env-secret-must-never-be-in-the-receipt"
# A Windows venv executable can be a launcher whose PID differs from the
# interpreter it starts. Fixture children with exact PID oracles use the
# actual base interpreter. run_owned targets deliberately keep sys.executable.
BASE_EXECUTABLE = str(Path(sys._base_executable).resolve(strict=True))

CONTROL = r'''
import json, os, pathlib, sys, time
path=pathlib.Path(sys.argv[1])
with path.open('x') as f: json.dump({'pid':os.getpid(),'role':'unrelated control'},f)
time.sleep(90)
'''

LEAF = r'''
import json, os, pathlib, sys, time
root=pathlib.Path(sys.argv[1])
with (root/'leaf.json').open('x') as f:
    json.dump({'pid':os.getpid(),'parent_pid':os.getppid(),'role':'acknowledged leaf'},f)
deadline=time.monotonic()+30
while not (root/'leaf-ack.txt').exists():
    if time.monotonic()>deadline: raise SystemExit(91)
    time.sleep(.005)
with (root/'leaf-ack-observed.txt').open('x') as f: f.write('leaf observed external acknowledgment\n')
while time.monotonic()<deadline: time.sleep(.02)
raise SystemExit(92)
'''

TARGET = r'''
import json, os, pathlib, subprocess, sys, time
root=pathlib.Path(sys.argv[1]); code=int(sys.argv[2])
options={'start_new_session':True} if os.name!='nt' else {
    'creationflags':subprocess.CREATE_NEW_PROCESS_GROUP|subprocess.CREATE_NO_WINDOW}
child=subprocess.Popen([sys._base_executable,'-I','-B',str(root/'leaf.py'),str(root)],**options)
deadline=time.monotonic()+20
while not (root/'leaf-ack-observed.txt').exists():
    if time.monotonic()>deadline: raise SystemExit(93)
    time.sleep(.005)
data=json.loads((root/'leaf.json').read_text())
if data['pid']!=child.pid or child.poll() is not None: raise SystemExit(94)
with (root/'target-ack.txt').open('x') as f: f.write('target acknowledged the still-live child\n')
raise SystemExit(code)
'''

MIDDLE = r'''
import os, pathlib, subprocess, sys, time
root=pathlib.Path(sys.argv[1])
options={'start_new_session':True} if os.name!='nt' else {
    'creationflags':subprocess.CREATE_NEW_PROCESS_GROUP|subprocess.CREATE_NO_WINDOW}
leaf=subprocess.Popen([sys._base_executable,'-I','-B',str(root/'leaf.py'),str(root)],**options)
deadline=time.monotonic()+20
while not (root/'leaf-ack-observed.txt').exists():
    if time.monotonic()>deadline: raise SystemExit(95)
    time.sleep(.005)
if leaf.poll() is not None: raise SystemExit(96)
with (root/'middle-exiting.txt').open('x') as f: f.write('intermediate exits while leaf remains alive\n')
'''

TIMEOUT_TARGET = r'''
import os, pathlib, subprocess, sys, time
root=pathlib.Path(sys.argv[1])
options={'start_new_session':True} if os.name!='nt' else {
    'creationflags':subprocess.CREATE_NEW_PROCESS_GROUP|subprocess.CREATE_NO_WINDOW}
middle=subprocess.Popen([sys._base_executable,'-I','-B',str(root/'middle.py'),str(root)],**options)
if middle.wait(timeout=20)!=0: raise SystemExit(97)
with (root/'middle-reaped.txt').open('x') as f: f.write('intermediate exited and was reaped before the timeout\n')
time.sleep(30)
'''


def wait_file(path, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if path.is_file():
            try:
                return json.loads(path.read_text())
            except ValueError:
                pass
        time.sleep(.005)
    raise AssertionError("Acknowledgment file was not completed: " + str(path))


class ProcessWitness:
    def __init__(self, pid):
        self.pid = pid
        self.handle = None
        self.pidfd = None
        if sys.platform == "win32":
            from ctypes import wintypes as W
            self.kernel = ctypes.WinDLL("kernel32", use_last_error=True, winmode=0x800)
            self.kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
            self.kernel.OpenProcess.restype = W.HANDLE
            self.kernel.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
            self.kernel.WaitForSingleObject.restype = W.DWORD
            self.kernel.CloseHandle.argtypes = [W.HANDLE]
            self.kernel.CloseHandle.restype = W.BOOL
            self.kernel.TerminateProcess.argtypes = [W.HANDLE, W.UINT]
            self.kernel.TerminateProcess.restype = W.BOOL
            self.handle = self.kernel.OpenProcess(0x100000 | 0x1000 | 0x0001, False, pid)
            if not self.handle:
                raise ctypes.WinError(ctypes.get_last_error())
        else:
            self.start = self._start_ticks()
            if callable(getattr(os, "pidfd_open", None)):
                try:
                    self.pidfd = os.pidfd_open(pid)
                except OSError:
                    pass  # Start-time identity remains the independent fallback.
        if self.exited():
            self.close()
            raise AssertionError("Child exited before its external acknowledgment")

    def _start_ticks(self):
        raw = Path(f"/proc/{self.pid}/stat").read_text()
        return int(raw[raw.rindex(")") + 2:].split()[19])

    def exited(self):
        if sys.platform == "win32":
            status = self.kernel.WaitForSingleObject(self.handle, 0)
            if status not in (0, 258):
                raise ctypes.WinError(ctypes.get_last_error())
            return status == 0
        if self.pidfd is not None:
            return bool(select.select([self.pidfd], [], [], 0)[0])
        try:
            # A reused PID does not denote the acknowledged child's lifetime.
            return self._start_ticks() != self.start
        except FileNotFoundError:
            return True

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None
        if self.pidfd is not None:
            os.close(self.pidfd)
            self.pidfd = None

    def terminate_windows_owned_lifetime(self):
        """Cleanup the exact retained fixture process, never a later PID reuse."""
        if sys.platform != "win32":
            raise RuntimeError("This cleanup requires a retained Windows process handle")
        if not self.exited():
            if not self.kernel.TerminateProcess(self.handle, 1):
                raise ctypes.WinError(ctypes.get_last_error())
            if self.kernel.WaitForSingleObject(self.handle, 10000) != 0:
                raise AssertionError("Owned fixture process did not exit during cleanup")


class OwnedCommandTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="imagejxx owned process ")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name).resolve()
        self.counter = 0
        self.control = None
        self.control_witness = None
        if SUPPORTED:
            marker = self.root / "control.json"
            options = {"start_new_session": True} if os.name != "nt" else {"creationflags": subprocess.CREATE_NO_WINDOW}
            self.control = subprocess.Popen([BASE_EXECUTABLE, "-I", "-B", "-c", CONTROL, str(marker)],
                                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                            stderr=subprocess.DEVNULL, **options)
            self.addCleanup(self.close_control)
            actual = wait_file(marker)
            # Retain the actual lifetime before any equality assertion. Even
            # an unexpected wrapper cannot orphan its interpreter on failure.
            self.control_witness = ProcessWitness(actual["pid"])
            self.assertEqual(actual["pid"], self.control.pid)
            self.assertIsNone(self.control.poll())

    def close_control(self):
        try:
            if self.control_witness is not None and sys.platform == "win32":
                self.control_witness.terminate_windows_owned_lifetime()
        finally:
            try:
                if self.control is not None:
                    if self.control.poll() is None:
                        self.control.kill()
                    self.control.wait(timeout=10)
            finally:
                if self.control_witness is not None:
                    self.control_witness.close()

    def require_supported(self):
        if not SUPPORTED:
            self.skipTest("Windows Job/Linux subreaper backend is unavailable on this host")

    def case(self, argv, *, timeout=10):
        self.counter += 1
        receipt = self.root / ("receipt-" + str(self.counter) + ".json")
        stdout = self.root / ("stdout-" + str(self.counter) + ".bin")
        stderr = self.root / ("stderr-" + str(self.counter) + ".bin")
        environment = dict(os.environ, IMAGEJXX_OWNED_TEST_SECRET=SECRET)
        try:
            with stdout.open("xb") as out, stderr.open("xb") as err:
                row = owned.run_owned(argv, cwd=self.root, env=environment, timeout=timeout,
                                      stdout=out, stderr=err, receipt_path=receipt)
        except owned.OwnedProcessError as error:
            self.assertEqual(json.loads(receipt.read_bytes()), error.receipt)
            self.assertNotIn(SECRET, receipt.read_text())
            if self.control is not None:
                self.assertIsNone(self.control.poll(), "Unrelated process was terminated")
            raise
        self.assertEqual(json.loads(receipt.read_bytes()), row)
        self.assertNotIn(SECRET, receipt.read_text())
        self.assertNotIn("env", row)
        self.assertIsNone(self.control.poll(), "Unrelated process was terminated")
        return row, stdout, stderr

    def witness_leaf(self):
        witnesses, errors = [], []

        def observe():
            try:
                data = wait_file(self.root / "leaf.json")
                self.assertEqual(data["role"], "acknowledged leaf")
                witness = ProcessWitness(data["pid"])
                witnesses.append(witness)
                (self.root / "leaf-ack.txt").write_text("external observer retained the live child\n")
            except BaseException as error:
                errors.append(error)

        thread = threading.Thread(target=observe, name="owned-child-witness", daemon=True)
        thread.start()
        return thread, witnesses, errors

    def finish_witness(self, observer):
        thread, witnesses, errors = observer
        thread.join(timeout=11)
        self.assertFalse(thread.is_alive(), "External child witness did not finish")
        self.assertEqual(errors, [])
        self.assertEqual(len(witnesses), 1)
        self.addCleanup(witnesses[0].close)
        self.assertTrue(witnesses[0].exited(), "Acknowledged child survived owned cleanup")
        return witnesses[0].pid

    def assert_cleanup(self, row):
        self.assertTrue(row["cleanup_complete"])
        self.assertEqual(row["cleanup"]["remaining"], [])
        if sys.platform == "win32":
            self.assertEqual(row["ownership"]["method"], "windows-job")
            self.assertTrue(row["ownership"]["assigned_before_resume"])
            self.assertTrue(row["ownership"]["handles_closed"])
            self.assertEqual(row["cleanup"]["terminal_wait"], "job_empty_and_target_signalled")
        else:
            self.assertEqual(row["ownership"]["method"], "linux-subreaper")
            self.assertTrue(row["ownership"]["subreaper_established"])
            self.assertNotEqual(row["ownership"]["supervisor_pid"], os.getpid())
            self.assertEqual(row["cleanup"]["terminal_wait"], "ECHILD")

    def test_normal_exit_preserves_streams_and_has_no_surviving_descendants(self):
        self.require_supported()
        row, out, err = self.case([sys.executable, "-I", "-B", "-c",
                                  "import sys;print('owned stdout');print('owned stderr',file=sys.stderr)"])
        self.assertEqual(row["state"], "exited")
        self.assertEqual(row["exit_code"], 0)
        self.assertTrue(row["target_completed"])
        self.assertFalse(row["descendants_remained_after_target_completion"],
                         "Normal-exit ownership receipt: " + json.dumps(row, sort_keys=True))
        self.assertEqual(out.read_text(), "owned stdout\n")
        self.assertEqual(err.read_text(), "owned stderr\n")
        self.assert_cleanup(row)

    @unittest.skipUnless(sys.platform == "win32", "Windows preserves the literal process exit code 259")
    def test_windows_exit_259_is_a_real_exit_not_a_timeout(self):
        row, _, _ = self.case([sys.executable, "-I", "-B", "-c", "raise SystemExit(259)"])
        self.assertEqual(row["exit_code"], 259)
        self.assertEqual(row["state"], "exited")
        self.assertFalse(row["timed_out"])
        self.assert_cleanup(row)

    @unittest.skipUnless(sys.platform == "win32", "Suspended Job assignment is a Windows ownership primitive")
    def test_windows_assignment_failure_terminates_suspended_child_before_execution(self):
        original = owned._WindowsJob

        class RefusedAssignment(original):
            def __init__(self):
                super().__init__()

                def refuse(job, process):
                    ctypes.set_last_error(5)
                    return 0

                self.kernel.AssignProcessToJobObject = refuse

        executed = self.root / "must-not-execute.txt"
        command = [sys.executable, "-I", "-B", "-c",
                   "from pathlib import Path;import sys;Path(sys.argv[1]).write_text('executed')", str(executed)]
        with mock.patch.object(owned, "_WindowsJob", RefusedAssignment):
            with self.assertRaises(owned.OwnedProcessError) as raised:
                self.case(command)
        row = raised.exception.receipt
        self.assertEqual(row["state"], "ownership_failed")
        self.assertIsNotNone(row["target_pid"])
        self.assertFalse(row["ownership"]["assigned_before_resume"])
        self.assertFalse(row["ownership"]["resumed"])
        self.assertFalse(executed.exists(), "Target ran before Job ownership was admitted")
        self.assertTrue(row["cleanup_complete"])
        self.assertEqual(row["cleanup"]["remaining"], [])
        self.assertEqual(row["cleanup"]["actions"], [{"action": "TerminateProcess", "pid": row["target_pid"],
                                                       "reason": "suspended_unassigned_child"}])
        self.assertTrue(row["ownership"]["handles_closed"])

    def survivor(self, code):
        self.require_supported()
        (self.root / "leaf.py").write_text(LEAF)
        (self.root / "target.py").write_text(TARGET)
        observer = self.witness_leaf()
        try:
            row, _, _ = self.case([sys.executable, "-I", "-B", str(self.root / "target.py"), str(self.root), str(code)])
        finally:
            pid = self.finish_witness(observer)
        self.assertEqual((self.root / "target-ack.txt").read_text(), "target acknowledged the still-live child\n")
        self.assertEqual(row["state"], "exited")
        self.assertEqual(row["exit_code"], code)
        self.assertTrue(row["target_completed"])
        self.assertTrue(row["descendants_remained_after_target_completion"])
        self.assertTrue(row["cleanup"]["actions"])
        self.assertIn(pid, [member["pid"] for member in row["cleanup"]["observed"]])
        self.assert_cleanup(row)

    def test_zero_exit_reports_and_removes_acknowledged_surviving_child(self):
        self.survivor(0)

    def test_nonzero_exit_is_preserved_while_surviving_child_is_removed(self):
        self.survivor(7)

    def test_timeout_removes_acknowledged_detached_double_fork_orphan(self):
        self.require_supported()
        for name, source in (("leaf.py", LEAF), ("middle.py", MIDDLE), ("timeout.py", TIMEOUT_TARGET)):
            (self.root / name).write_text(source)
        observer = self.witness_leaf()
        try:
            with self.assertRaises(owned.OwnedProcessTimeout) as raised:
                self.case([sys.executable, "-I", "-B", str(self.root / "timeout.py"), str(self.root)], timeout=2)
            row = raised.exception.receipt
        finally:
            pid = self.finish_witness(observer)
        self.assertEqual((self.root / "middle-reaped.txt").read_text(),
                         "intermediate exited and was reaped before the timeout\n")
        self.assertEqual(row["state"], "timed_out")
        self.assertTrue(row["timed_out"])
        self.assertFalse(row["target_completed"])
        self.assertIn(pid, [member["pid"] for member in row["cleanup"]["observed"]])
        self.assert_cleanup(row)
        self.assertLess(row["elapsed_seconds"], 2 + owned.CLEANUP_SECONDS + 2)

    def test_failed_launch_is_durable_and_leaves_no_owned_child(self):
        self.require_supported()
        with self.assertRaises(owned.OwnedProcessError) as raised:
            self.case([str(self.root / "nonexistent-owned-command")])
        row = raised.exception.receipt
        self.assertEqual(row["state"], "launch_failed")
        self.assertIsNone(row["target_pid"])
        self.assertIsNone(row["exit_code"])
        self.assertTrue(row["cleanup_complete"])
        self.assertEqual(row["cleanup"]["remaining"], [])
        self.assertEqual(row["cleanup"]["actions"], [])

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux parent-death signal/subreaper lifecycle")
    def test_owner_death_stops_and_reaps_live_detached_descendants(self):
        for name, source in (("leaf.py", LEAF), ("middle.py", MIDDLE), ("timeout.py", TIMEOUT_TARGET)):
            (self.root / name).write_text(source)
        receipt = self.root / "abandoned-owner.json"
        owner_script = self.root / "owner.py"
        owner_script.write_text(
            "import importlib.util,os,subprocess,sys\nfrom pathlib import Path\n"
            "root=Path(sys.argv[1])\n"
            "spec=importlib.util.spec_from_file_location('fixture_owner',sys.argv[2])\n"
            "module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)\n"
            "with (root/'abandoned-target.log').open('wb') as log:\n"
            " module.run_owned([sys.executable,'-I','-B',str(root/'timeout.py'),str(root)],"
            "cwd=root,env=dict(os.environ),timeout=30,stdout=log,stderr=subprocess.STDOUT,"
            "receipt_path=root/'abandoned-owner.json')\n")
        observer = self.witness_leaf()
        supervisor = None
        with (self.root / "abandoned-owner.log").open("wb") as log:
            owner = subprocess.Popen([sys.executable, "-I", "-B", str(owner_script), str(self.root), str(HELPER)],
                                     stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                deadline = time.monotonic() + 10
                while not (self.root / "middle-reaped.txt").exists() and time.monotonic() < deadline:
                    self.assertIsNone(owner.poll())
                    time.sleep(.01)
                self.assertTrue((self.root / "middle-reaped.txt").exists())
                running = wait_file(receipt)
                self.assertEqual(running["ownership"]["caller_pid"], owner.pid)
                self.assertTrue(running["ownership"]["subreaper_established"])
                supervisor = ProcessWitness(running["ownership"]["supervisor_pid"])
                self.addCleanup(supervisor.close)
                owner.kill()
                owner.wait(timeout=5)
                deadline = time.monotonic() + owned.CLEANUP_SECONDS + 2
                while not supervisor.exited() and time.monotonic() < deadline:
                    time.sleep(.01)
                self.assertTrue(supervisor.exited(), "Ownerless supervisor did not finish bounded cleanup")
                row = json.loads(receipt.read_bytes())
                self.assertEqual(row["state"], "supervisor_failed", row)
                self.assertEqual(row["ownership"]["parent_death_signal"], 15)
                self.assert_cleanup(row)
                self.assertIsNone(self.control.poll(), "Owner death affected an unrelated process")
            finally:
                if owner.poll() is None:
                    owner.kill()
                    owner.wait(timeout=5)
        self.finish_witness(observer)

    def test_unsupported_platform_refuses_before_any_process_launch(self):
        receipt = self.root / "unsupported.json"
        with mock.patch.object(owned.sys, "platform", "darwin"), mock.patch.object(
                owned.subprocess, "Popen", side_effect=AssertionError("Unsupported host launched a child")):
            with self.assertRaises(owned.OwnedProcessUnsupported) as raised:
                owned.run_owned([sys.executable, "-I", "-c", "raise SystemExit(0)"], cwd=self.root,
                                env=dict(os.environ), timeout=1, stdout=None, stderr=None, receipt_path=receipt)
        self.assertEqual(raised.exception.receipt["state"], "unsupported")
        self.assertFalse(raised.exception.receipt["ownership"]["child_launched"])
        self.assertEqual(json.loads(receipt.read_bytes()), raised.exception.receipt)

    def test_invalid_arguments_and_reused_receipt_refuse_before_launch(self):
        receipt = self.root / "existing.json"
        receipt.write_bytes(b"retain this existing receipt\n")
        with mock.patch.object(owned.subprocess, "Popen", side_effect=AssertionError("Invalid input launched a child")):
            for timeout in (False, 0, -1, float("nan"), float("inf")):
                with self.subTest(timeout=timeout), self.assertRaises(ValueError):
                    owned.run_owned([sys.executable], cwd=self.root, env=dict(os.environ), timeout=timeout,
                                    stdout=None, stderr=None, receipt_path=self.root / "invalid.json")
            with self.assertRaises(ValueError):
                owned.run_owned([sys.executable], cwd=self.root, env=dict(os.environ), timeout=1,
                                stdout=None, stderr=None, receipt_path=receipt)
        self.assertEqual(receipt.read_bytes(), b"retain this existing receipt\n")
        self.assertFalse((self.root / "invalid.json").exists())


if __name__ == "__main__":
    unittest.main()
