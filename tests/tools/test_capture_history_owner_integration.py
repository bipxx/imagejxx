"""Two real Windows/Linux history-owner controls, with no ImageJXX process.

The child is an isolated standard-library Python script. The overflow case
uses the real watchdog interruption and existing process owner; neither is
mocked. There are no detached children or test-authored PID signals.

Each case creates a fresh .tmp/hci-* directory and retains its small inputs,
logs, terminal ownership receipt and result for the root verifier. This test
does not delete scratch, including on failure or unresolved ownership. Root
must verify/retain the evidence before retiring these exact directories.
Darwin requires its separate actual broker qualification and is not claimed.
"""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import threading
import time
import unittest


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/capture_shortcuts_history_ui.py"
SPEC = importlib.util.spec_from_file_location("_history_real_owner_capture", SCRIPT)
capture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(capture)
SUPPORTED = sys.platform in ("win32", "linux")
CHILD = r'''import json, os, pathlib, sys, time
root = pathlib.Path(sys.argv[1])
mode = sys.argv[2]
with (root / 'child.json').open('x', encoding='utf-8') as stream:
    json.dump({'pid': os.getpid(), 'parent_pid': os.getppid(), 'mode': mode}, stream)
    stream.write('\n')
if mode == 'success':
    sys.stdout.buffer.write(b'owned-history-success\n')
    sys.stdout.buffer.flush()
    raise SystemExit(0)
if mode != 'overflow':
    raise SystemExit(91)
sys.stdout.buffer.write(b'x' * (128 * 1024))
sys.stdout.buffer.flush()
time.sleep(30)
raise SystemExit(92)
'''


def save(path, value):
    raw = (json.dumps(value, sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")
    if len(raw) > 256 * 1024:
        raise ValueError("History ownership test metadata exceeds256KiB")
    with path.open("xb") as stream:
        stream.write(raw)


@unittest.skipUnless(SUPPORTED, "Real history owner controls require Windows or Linux; Darwin is separate")
class HistoryOwnerIntegration(unittest.TestCase):
    def setUp(self):
        parent = ROOT / ".tmp"
        if not parent.exists():
            parent.mkdir()
        info = parent.lstat()
        if (not stat.S_ISDIR(info.st_mode) or stat.S_ISLNK(info.st_mode)
                or getattr(info, "st_file_attributes", 0) & 0x400):
            raise RuntimeError("History integration scratch parent must be a regular owned directory")
        self.root = Path(tempfile.mkdtemp(prefix="hci-", dir=parent)).resolve(strict=True)
        self.assertEqual(self.root.parent, parent.resolve(strict=True))
        self.assertTrue(self.root.name.startswith("hci-"))
        self.child = self.root / "child.py"
        self.child.write_text(CHILD, encoding="utf-8", newline="\n")
        self.engine = capture.engine(portable=True)
        self.owner = capture.history_owner()
        self.report = {"processes": [], "cases": [], "limits": {"stage_bytes": 64 * 1024}}
        self.inputs = {
            "schema": "imagejxx.history-owner-integration-inputs.001", "case_id": self.id(),
            "scope": "real standard-library child/current owner/watchdog; no product",
            "platform": sys.platform, "acceptance_claim": False,
            "timeout_seconds": 10, "stage_bytes": 64 * 1024, "overflow_stdout_bytes": 128 * 1024,
            "child": capture.pin(self.child), "python_engine": capture.pin(self.engine),
            "sources": {str(path.relative_to(ROOT)): capture.pin(path) for path in (
                Path(__file__).resolve(), SCRIPT, ROOT / "scripts/ci_owned_process.py")},
        }
        save(self.root / "test-inputs.json", self.inputs)
        print("HISTORY_OWNER_INTEGRATION_SCRATCH=" + str(self.root), flush=True)

    def run_child(self, mode):
        argv = [str(self.engine), "-I", "-X", "utf8", "-B", str(self.child), str(self.root), mode]
        return capture.run_history_owned(self.owner, argv, self.root, "case",
                                         time.monotonic() + 10., self.report)

    def terminal(self):
        row = self.report["processes"][0]
        receipt = Path(row["owned_process_receipt"]["path"])
        self.assertEqual(receipt, self.root / "case.process.json")
        self.assertEqual(capture.pin(receipt), row["owned_process_receipt"])
        return capture.read_json(receipt)

    def assert_resolved(self, terminal):
        child = capture.read_json(self.root / "child.json")
        self.assertIs(type(terminal["target_pid"]), int)
        self.assertEqual(terminal["target_pid"], child["pid"])
        self.assertEqual(terminal["schema"], "imagejxx.owned-command.001")
        self.assertIs(terminal["cleanup_complete"], True)
        self.assertEqual(terminal["cleanup"]["remaining"], [])
        self.assertIsNone(terminal.get("supervisor_pid_if_unresolved"))
        self.assertNotEqual(terminal["state"], "running")
        self.assertIs(type(terminal["exit_code"]), int)
        if sys.platform == "win32":
            self.assertEqual(terminal["ownership"]["method"], "windows-job")
            self.assertIs(terminal["ownership"]["assigned_before_resume"], True)
            self.assertIs(terminal["ownership"]["handles_closed"], True)
            self.assertEqual(terminal["cleanup"]["terminal_wait"], "job_empty_and_target_signalled")
            waited = [row for row in terminal["cleanup"]["waited"] if row["pid"] == child["pid"]]
            self.assertEqual(len(waited), 1)
            self.assertEqual(waited[0]["exit_code"], terminal["exit_code"])
        else:
            self.assertEqual(terminal["ownership"]["method"], "linux-subreaper")
            self.assertIs(terminal["ownership"]["subreaper_established"], True)
            self.assertEqual(terminal["ownership"]["supervisor_pid"], child["parent_pid"])
            self.assertNotEqual(terminal["ownership"]["supervisor_pid"], os.getpid())
            self.assertEqual(terminal["cleanup"]["terminal_wait"], "ECHILD")
            if "supervisor_exit_code" in terminal:
                self.assertEqual(terminal["supervisor_exit_code"], 0)
        self.assertFalse(any(row.name == "history-storage-monitor" for row in threading.enumerate()))

    def retain(self, error):
        result = {"schema": "imagejxx.history-owner-integration-result.001", "case_id": self.id(),
                  "state": "passed" if error is None else "failed", "acceptance_claim": False,
                  "scope": self.inputs["scope"], "scratch_retained": str(self.root),
                  "capture_processes": self.report["processes"], "accepted_descriptors": self.report["cases"],
                  "error": None if error is None else {"type": type(error).__name__, "message": str(error)},
                  "members": []}
        for name in ("test-inputs.json", "child.py", "child.json", "case.stdout", "case.stderr", "case.process.json"):
            path = self.root / name
            if path.exists():
                result["members"].append({"name": name, **capture.pin(path, allow_empty=True)})
        save(self.root / "result.json", result)
        if error is not None:
            print("HISTORY_OWNER_INTEGRATION_FAILED=" + str(self.root), file=sys.stderr, flush=True)
            for name, limit in (("case.process.json", 32768), ("case.stderr", 4096)):
                path = self.root / name
                if path.is_file():
                    with path.open("rb") as stream:
                        raw = stream.read(limit + 1)
                    text = raw[:limit].decode("utf-8", errors="replace")
                    if len(raw) > limit:
                        text += "\n[log excerpt bounded; complete original file retained]"
                    print("HISTORY OWNER DIAGNOSTIC " + name + ":\n" + text, file=sys.stderr, flush=True)

    def test_real_owned_success(self):
        error = None
        try:
            row = self.run_child("success")
            terminal = self.terminal()
            self.assertEqual(row["state"], "passed")
            self.assertEqual(terminal["state"], "exited")
            self.assertEqual(terminal["exit_code"], 0)
            self.assertIs(terminal["target_completed"], True)
            self.assertIs(terminal["timed_out"], False)
            self.assert_resolved(terminal)
            self.assertEqual((self.root / "case.stdout").read_bytes(), b"owned-history-success\n")
            self.assertEqual(row["live_storage_monitor"]["state"], "stopped")
            self.assertIsNone(row["live_storage_monitor"]["first_violation"])
        except BaseException as caught:
            error = caught
            raise
        finally:
            self.retain(error)

    def test_real_watchdog_overflow_interrupts_and_drains(self):
        error = None
        try:
            with self.assertRaises(RuntimeError):
                self.run_child("overflow")
            self.assertEqual(len(self.report["processes"]), 1)
            row = self.report["processes"][0]
            self.assertEqual(row["state"], "failed")
            self.assertEqual(row["live_storage_monitor"]["state"], "failed")
            violation = row["live_storage_monitor"]["first_violation"]
            self.assertEqual(violation["type"], "RuntimeError")
            self.assertEqual(violation["message"], "Owned output exceeds byte/file/log bounds")
            self.assertEqual((self.root / "case.stdout").stat().st_size, 128 * 1024)
            terminal = self.terminal()
            self.assert_resolved(terminal)
            self.assertIs(terminal["target_completed"], False)
            self.assertIs(terminal["timed_out"], False)
            self.assertEqual(terminal["error"]["type"], "KeyboardInterrupt")
            self.assertNotEqual(terminal["exit_code"], 0)
            self.assertTrue(terminal["cleanup"]["actions"])
            self.assertEqual(self.report["cases"], [])
            self.assertFalse((self.root / "provenance.json").exists())
            self.assertFalse((self.root / "source-product-binding.json").exists())
        except BaseException as caught:
            error = caught
            raise
        finally:
            self.retain(error)


if __name__ == "__main__":
    unittest.main()
