"""Bounded Ninja console-pool experiment, not product-build qualification.

Five native cases execute a real console edge; the sixth refuses a nonconsole
graph before execution. The original eight Python-only lifecycle cases are not
inherited. Run explicitly on Darwin with --require-darwin and an absolute
IMAGEJXX_NINJA_CONSOLE_CONTROL_EXE pointing to Ninja 1.13.2. No downloads,
compiler, CMake configuration, product imports or arbitrary graph admission.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import signal
import stat
import subprocess
import sys
import unittest
from unittest.mock import patch


BASE_PATH = Path(__file__).with_name("test_ci_mac_process.py")
SPEC = importlib.util.spec_from_file_location("mac_ninja_fixture", BASE_PATH)
base = importlib.util.module_from_spec(SPEC)
exec(compile(BASE_PATH.read_bytes(), str(BASE_PATH), "exec", dont_inherit=True), base.__dict__)
mac = base.mac
NINJA_ENV = "IMAGEJXX_NINJA_CONSOLE_CONTROL_EXE"


def require_native():
    if sys.platform != "darwin":
        raise RuntimeError("Ninja console ownership requires the actual Darwin kernel")
    if not os.environ.get(NINJA_ENV):
        raise RuntimeError("An explicit Ninja 1.13.2 executable is required")


def pin(path):
    path = Path(path).resolve(strict=True)
    info = path.stat()
    if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= 64 * 1024 * 1024:
        raise ValueError("Control input must be a bounded regular file")
    return {"path": str(path), "bytes": info.st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def console_graph(argv):
    # A single literal edge. exec removes the intermediate shell; no general
    # Ninja parser or claim about a generated CMake graph is made here.
    command = shlex.join(argv).replace("$", "$$")
    return ("ninja_required_version = 1.13.2\nrule payload\n  command = exec " + command
            + "\n  pool = console\nbuild run: payload\ndefault run\n")


def admit_graph(observed, argv):
    if observed != console_graph(argv):
        raise ValueError("Only the exact single console edge is admitted")


class ConsoleGraphMetadata(unittest.TestCase):
    def test_exact_graph_accepts_and_nonconsole_or_extra_edge_refuses(self):
        argv = ["/literal path/python", "-I", "/literal/$/payload.py"]
        text = console_graph(argv)
        admit_graph(text, argv)
        for changed in (text.replace("  pool = console\n", ""),
                        text.replace("pool = console", "pool = other"), text + "build extra: payload\n"):
            with self.subTest(graph=changed), self.assertRaisesRegex(ValueError, "exact single console"):
                admit_graph(changed, argv)

    def test_required_native_entry_refuses_other_platforms_and_missing_tool(self):
        with patch.object(sys, "platform", "linux"), self.assertRaisesRegex(RuntimeError, "actual Darwin"):
            require_native()
        with patch.object(sys, "platform", "darwin"), patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, "explicit Ninja"):
                require_native()


@unittest.skipUnless(sys.platform == "darwin" and bool(os.environ.get(NINJA_ENV)),
                     "Explicit actual-Darwin Ninja 1.13.2 experiment; metadata is not native evidence")
class DarwinNinjaLifecycle(unittest.TestCase):
    tool_proof = None

    def setUp(self):
        base.DarwinLifecycle.setUp(self)
        self.addCleanup(self.report_control)
        require_native()
        requested = Path(os.environ[NINJA_ENV])
        self.assertTrue(requested.is_absolute())
        self.ninja_pin = pin(requested)
        self.ninja = self.ninja_pin["path"]
        self.assertTrue(os.access(self.ninja, os.X_OK))
        if type(self).tool_proof is None:
            try:
                with (self.root / "version.log").open("xb") as log:
                    row = mac.run_mac_owned([self.ninja, "--version"], cwd=self.root, env=os.environ.copy(),
                        timeout=5, stdout=log, stderr=subprocess.STDOUT, receipt_path=self.root / "version.json")
            except mac.OwnedProcessError as error:
                self.independent_cleanup_complete = error.receipt.get("cleanup_complete") is True
                raise
            self.assertEqual((row["state"], row["exit_code"], row["target_completed"], row["cleanup_complete"]),
                             ("exited", 0, True, True))
            self.assertEqual((self.root / "version.log").read_text().strip(), "1.13.2")
            type(self).tool_proof = {"pin": self.ninja_pin, "version": "1.13.2", "process": row}
        self.assertEqual(self.ninja_pin, type(self).tool_proof["pin"])
        self.nonconsole = False

    def report_control(self):
        evidence = {"scope": "Darwin single-edge Ninja control only", "case": self.id(),
                    "product_acceptance": False, "tool": type(self).tool_proof, "files": {}}
        for name in ("owned.json", "version.json", "independent-witness.json", "edge.json"):
            path = self.root / name
            if path.is_file():
                self.assertLessEqual(path.stat().st_size, 256 * 1024)
                evidence["files"][name] = json.loads(path.read_bytes())
                if name in ("owned.json", "version.json") and evidence["files"][name].get("cleanup_complete") is not True:
                    self.independent_cleanup_complete = False
        graph = self.root / "build.ninja"
        if graph.is_file():
            evidence["graph"] = {"pin": pin(graph), "text": graph.read_text()}
        if hasattr(self, "ninja_pin"):
            self.assertEqual(pin(self.ninja), self.ninja_pin)
        print("NINJA_CONSOLE_CONTROL " + json.dumps(evidence, sort_keys=True), flush=True)

    def assert_complete(self, row):
        base.DarwinLifecycle.assert_complete(self, row)
        self.assertEqual(row["argv"], [self.ninja, "-f", str(self.root / "build.ninja"), "-j1", "run"])
        self.assertEqual(len(row["ownership"]["groups"]), 1)

    def launcher(self, argv):
        graph = self.root / "build.ninja"
        text = console_graph(argv)
        if self.nonconsole:
            text = text.replace("  pool = console\n", "")
        graph.write_text(text, encoding="utf-8")
        admit_graph(graph.read_text(encoding="utf-8"), argv)
        return [self.ninja, "-f", str(graph), "-j1", "run"]

    def run_exit(self, code):
        source = ("import json,os,sys\nfrom pathlib import Path\n"
                  "Path(sys.argv[1],'edge.json').write_text(json.dumps({'pid':os.getpid(),"
                  "'parent_pid':os.getppid(),'pgid':os.getpgrp(),'sid':os.getsid(0),"
                  "'payload_exit':" + repr(code) + "}))\nsys.exit(" + repr(code) + ")\n")
        row = self.run_fixture(source, launcher=self.launcher)
        self.assert_complete(row)
        edge = json.loads((self.root / "edge.json").read_bytes())
        self.assertNotEqual(edge["pid"], row["target_pid"])
        self.assertEqual((edge["parent_pid"], edge["pgid"], edge["sid"], edge["payload_exit"]),
            (row["target_pid"], row["target_pid"], row["ownership"]["session_id"], code))
        self.assertEqual((row["state"], row["exit_code"], row["target_completed"]), ("exited", 0 if code == 0 else 1, True))
        self.assertFalse(row["timed_out"])
        self.assertFalse(row["descendants_remained_after_target_completion"])

    def test_console_normal_exit(self):
        self.run_exit(0)

    def test_console_nonzero_exit(self):
        self.run_exit(7)

    def test_console_surviving_leaf_after_ninja_exit(self):
        state = self.independent_fixture(launcher=self.launcher)
        (self.root / "release-target").write_text("release console payload", encoding="utf-8")
        self.assertEqual(state["owner"].wait(timeout=15), 0)
        row = self.assert_independent_owned_exit(state)
        self.assertEqual((row["state"], row["exit_code"], row["target_completed"]), ("exited", 0, True))
        self.assertTrue(row["descendants_remained_after_target_completion"])

    def test_console_timeout(self):
        state = self.independent_fixture(launcher=self.launcher, timeout=15)
        self.assertEqual(state["owner"].wait(timeout=30), 1)
        row = self.assert_independent_owned_exit(state)
        self.assertEqual(row["state"], "timed_out")
        self.assertTrue(row["timed_out"])
        self.assertFalse(row["target_completed"])

    def test_console_owner_death(self):
        state = self.independent_fixture(launcher=self.launcher)
        self.assertIsNone(state["owner"].poll())
        state["owner"].kill()  # Only this retained direct owner; never a sampled PID.
        self.assertEqual(state["owner"].wait(timeout=5), -signal.SIGKILL)
        row = self.assert_independent_owned_exit(state)
        self.assertEqual(row["state"], "supervisor_failed")
        self.assertFalse(row["timed_out"])
        self.assertIn("owner pipe closed", row["error"]["message"])

    def test_nonconsole_graph_refused_before_target_launch(self):
        self.nonconsole = True
        with patch.object(mac, "run_mac_owned") as launch, self.assertRaisesRegex(ValueError, "exact single console"):
            self.run_fixture("raise RuntimeError('nonconsole edge must not execute')\n", launcher=self.launcher)
        launch.assert_not_called()
        self.assertFalse(self.receipt.exists())
        self.assertFalse((self.root / "edge.json").exists())


# Reuse fixture helpers only: do not inherit eight non-Ninja test methods.
for name in ("retire_temporary", "failure_diagnostics", "wait_json", "independent_fixture",
             "assert_independent_control_live", "cleanup_independent_fixture",
             "assert_independent_owned_exit", "run_fixture"):
    setattr(DarwinNinjaLifecycle, name, getattr(base.DarwinLifecycle, name))


if __name__ == "__main__":
    if "--require-darwin" in sys.argv[1:]:
        if sys.argv[1:] != ["--require-darwin"]:
            raise SystemExit("The native selector accepts only --require-darwin")
        require_native()
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(DarwinNinjaLifecycle))
        raise SystemExit(0 if result.wasSuccessful() and result.testsRun == 6 and not result.skipped else 1)
    unittest.main()
