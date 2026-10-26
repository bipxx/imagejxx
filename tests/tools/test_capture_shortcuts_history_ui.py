"""Synthetic transport controls; no product, compiler, or child is executed.

Passed-build rows below are explicitly synthetic test data. They test the
capture's binding checks, not a real build or any history/scientific outcome.
"""
from __future__ import annotations

import ast
from contextlib import ExitStack
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest import mock


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/capture_shortcuts_history_ui.py"
SPEC = importlib.util.spec_from_file_location("_history_capture_transport", SCRIPT)
capture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(capture)
PAIRS = [(case, context) for case in ("HU01", "HU02", "HU03", "HU04")
         for context in ("imgui_cpp", "imgui_python")]


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, allow_nan=False, sort_keys=True) + "\n", encoding="utf-8")


def put(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(raw)
    return capture.pin(path)


class InstalledHistoryTransport(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="history-transport-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name).resolve(strict=True)
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.stack.enter_context(mock.patch.object(capture, "ROOT", self.root))
        self.calls, self.raw_bytes = [], {}
        self.serial = 0
        roles = capture.COMMON | capture.SPEC["history"]["roles"]
        sources = []
        for logical in sorted(set(roles.values())):
            sources.append({"logical_path": logical, **put(
                self.root / logical, ("SYNTHETIC SOURCE ONLY: " + logical + "\n").encode())})
        self.inputs = self.root / "inputs"
        self.inputs.mkdir()
        self.freeze_path = self.inputs / "source-freeze.json"
        self.freeze = {"schema_version": 1, "fixture": "synthetic-transport-only", "sources": sources}
        save(self.freeze_path, self.freeze)
        self.cpp_root, self.site = self.root / "installed-cpp", self.root / "installed-python"
        self.app = self.cpp_root / "bin/imagejxx"
        self.engine = self.root / "engine/python"
        engine = put(self.engine, b"SYNTHETIC INTERPRETER, NEVER EXECUTED\n")
        app = put(self.app, b"SYNTHETIC CPP PRODUCT, NEVER EXECUTED\n")
        modules = {}
        module_paths = {
            "imagejxx": "imagejxx/__init__.py", "imagejxx._native": "imagejxx/_native.synthetic",
            "imagejxx._desktop": "imagejxx/_desktop.synthetic", "imagejxx.ui.app": "imagejxx/ui/app.py",
            "imagejxx.ui.persistence": "imagejxx/ui/persistence.py",
        }
        source_modules = {
            "imagejxx": capture.COMMON["python_package"],
            "imagejxx.ui.app": capture.COMMON["python_wrapper"],
            "imagejxx.ui.persistence": capture.COMMON["python_preferences"],
        }
        for name, relative in module_paths.items():
            raw = ((self.root / source_modules[name]).read_bytes() if name in source_modules
                   else ("SYNTHETIC COMPILED MODULE: " + name + "\n").encode())
            modules[name] = put(self.site / relative, raw)
        built = {
            "cpp": put(self.root / "cache/app.bin", self.app.read_bytes()),
            "_native": put(self.root / "cache/native.bin", Path(modules["imagejxx._native"]["path"]).read_bytes()),
            "_desktop": put(self.root / "cache/desktop.bin", Path(modules["imagejxx._desktop"]["path"]).read_bytes()),
        }
        products = {
            "imgui_cpp": {role: app for role in capture.RUNTIME_ROLES},
            "imgui_python": {"executable": engine, "producer_artifact": modules["imagejxx._desktop"],
                             "native_artifact": modules["imagejxx._desktop"]},
        }
        self.build_path = self.inputs / "build.json"
        self.build = {"fixture": "synthetic-transport-only", "state": "passed",
                      "source_freeze": capture.pin(self.freeze_path), "built_products": built}
        save(self.build_path, self.build)
        self.numpy_site = self.root / "dependencies"
        numpy = put(self.numpy_site / "numpy/__init__.py", b"SYNTHETIC NUMPY, NEVER IMPORTED\n")
        self.bindings_path = self.inputs / "bindings.json"
        self.bindings = {
            "schema_version": 1, "kind": "p06h.installed-ui-capture-bindings.001",
            "kind_selection": "history", "fixture": "synthetic-transport-only",
            "source_freeze": capture.pin(self.freeze_path), "build_receipt": capture.pin(self.build_path),
            "built_products": built, "products": products, "installed_modules": modules,
            "python_engine": engine,
            "numpy": {"site_packages": str(self.numpy_site), "module": numpy, "version": "2.2.6"},
        }
        save(self.bindings_path, self.bindings)
        (self.root / ".tmp").mkdir()
        self.engine_mock = self.stack.enter_context(mock.patch.object(capture, "engine", return_value=self.engine))
        self.stack.enter_context(mock.patch.object(capture, "history_owner", return_value=self.owner))
        self.change_raw = self.change_terminal = self.after_case = None

    def arguments(self):
        self.serial += 1
        return SimpleNamespace(kind="history", case_ids=None,
                               output=self.root / ".tmp" / ("capture-" + str(self.serial)),
                               cpp_prefix=self.cpp_root, python_site_packages=self.site,
                               numpy_site_packages=self.numpy_site, build_receipt=self.build_path,
                               source_freeze=self.freeze_path, product_bindings=self.bindings_path)

    def rebind_fixture(self):
        save(self.freeze_path, self.freeze)
        self.build["source_freeze"] = capture.pin(self.freeze_path)
        save(self.build_path, self.build)
        self.bindings["source_freeze"] = capture.pin(self.freeze_path)
        self.bindings["build_receipt"] = capture.pin(self.build_path)
        save(self.bindings_path, self.bindings)

    def owner(self, argv, **options):
        """A metadata-only stand-in, deliberately not a real process owner."""
        context = "imgui_cpp" if argv[0] == str(self.app) else "imgui_python"
        if context == "imgui_cpp":
            case, directory = argv[-2], Path(argv[-1])
        else:
            case = argv[argv.index("--python-case") + 1]
            directory = Path(argv[argv.index("--directory") + 1])
        self.calls.append((case, context, list(argv), options))
        directory.mkdir()
        pid = 10000 + len(self.calls)
        runtime = {"process_id": pid, **self.bindings["products"][context]}
        raw = {"schema": "imagejxx.history-settings-ui-observations", "version": 1,
               "contract": "p06h.history-settings-ui.001", "case_id": case, "context": context,
               "observation_state": "observed", "acceptance_claim": False, "frames": [{}],
               "elapsed_seconds": .001, "runtime_before": runtime, "runtime_after": runtime,
               "synthetic_transport_fixture": True}
        if self.change_raw:
            self.change_raw(raw)
        raw_path = directory / "report.json"
        save(raw_path, raw)
        self.raw_bytes[str(raw_path)] = raw_path.read_bytes()
        if context == "imgui_python":
            launch = capture.read_json(Path(argv[argv.index("--launch-inputs") + 1]))
            save(directory.parent / "launcher-origin.json", {
                "schema_version": 1, "kind": "p06h.history-settings-python-launcher-origin.001",
                "case_id": case, "context": context, "process_id": pid,
                "source_set_sha256": launch["source_set_sha256"],
                "before": self.bindings["products"][context], "after": self.bindings["products"][context],
                "modules_before": self.bindings["installed_modules"],
                "modules_after": self.bindings["installed_modules"],
                "synthetic_transport_fixture": True,
            })
        terminal = {"schema": "imagejxx.owned-command.001", "state": "exited", "exit_code": 0,
                    "target_pid": pid, "target_completed": True, "cleanup_complete": True,
                    "timed_out": False, "fixture": "synthetic-transport-only"}
        if self.change_terminal:
            self.change_terminal(terminal)
        options["stdout"].write(b"SYNTHETIC TRANSPORT ONLY\n")
        save(options["receipt_path"], terminal)
        if self.after_case:
            self.after_case(raw_path, terminal)
        return terminal

    def test_exact_eight_original_identities_layout_and_isolated_arguments(self):
        args = self.arguments()
        capture.capture(args)
        self.assertEqual([(row[0], row[1]) for row in self.calls], PAIRS)
        result = capture.read_json(args.output / "capture.json")
        provenance = capture.read_json(args.output / "provenance.json")
        self.assertEqual(result["state"], "passed")
        self.assertIs(result["acceptance_claim"], False)
        self.assertEqual([(row["id"], row["context"]) for row in provenance["cases"]], PAIRS)
        self.assertEqual(result["limits"]["frames"], 180)
        self.assertEqual(result["limits"]["producer_seconds"], 15)
        self.assertEqual(result["limits"]["raw_bytes"], 16 * capture.MIB)
        self.assertEqual(result["limits"]["paired_raw_bytes"], 32 * capture.MIB)
        for case, context, argv, options in self.calls:
            self.assertEqual(options["cwd"], args.output)
            self.assertGreater(options["timeout"], 0)
            self.assertLessEqual(options["timeout"], 15)
            if context == "imgui_cpp":
                self.assertEqual(argv[1:3], ["--history-settings-ui-case", case])
            else:
                self.assertEqual(argv[1:5], ["-I", "-X", "utf8", "-B"])
                self.assertEqual(Path(argv[5]), args.output / "source" / capture.COMMON["python_launcher"])
            raw = args.output / context / case / "run/report.json"
            self.assertEqual(raw.read_bytes(), self.raw_bytes[str(raw)])
        for process in result["processes"]:
            terminal = capture.read_json(Path(process["owned_process_receipt"]["path"]))
            self.assertEqual(process["pid"], terminal["target_pid"])
            self.assertEqual(process["live_storage_monitor"]["state"], "stopped")
        self.engine_mock.assert_called_once_with(portable=True)

    def test_original_reader_roles_are_retained_with_pinned_owner_roles(self):
        tree = ast.parse((ROOT / "tests/python/test_history_settings_ui.py").read_text(encoding="utf-8"))
        required = next(ast.literal_eval(node.value) for node in tree.body if isinstance(node, ast.Assign)
                        and any(isinstance(name, ast.Name) and name.id == "SOURCE_ROLES" for name in node.targets))
        args = self.arguments()
        capture.capture(args)
        rows = capture.read_json(args.output / "provenance.json")["sources"]
        roles = {row["role"]: row["logical_path"] for row in rows}
        self.assertEqual({role: roles[role] for role in required}, required)
        self.assertEqual(roles["process_helper"], "scripts/ci_owned_process.py")
        self.assertEqual(roles["mac_process_helper"], "scripts/ci_mac_process.py")
        self.assertLessEqual(len(rows), 128)

    def test_history_cannot_select_a_partial_membership(self):
        args = self.arguments()
        args.case_ids = ["HU01"]
        with self.assertRaisesRegex(RuntimeError, "canonical orientation"):
            capture.capture(args)
        self.assertFalse(self.calls)
        self.assertFalse(args.output.exists())

    def test_unpassed_build_refuses_before_any_launch(self):
        self.build["state"] = "failed"
        self.rebind_fixture()
        with self.assertRaisesRegex(RuntimeError, "Passed build"):
            capture.capture(self.arguments())
        self.assertFalse(self.calls)

    def test_build_freeze_mismatch_refuses_before_any_launch(self):
        self.build["source_freeze"] = {**self.build["source_freeze"], "sha256": "0" * 64}
        save(self.build_path, self.build)
        self.bindings["build_receipt"] = capture.pin(self.build_path)
        save(self.bindings_path, self.bindings)
        with self.assertRaisesRegex(RuntimeError, "Passed build"):
            capture.capture(self.arguments())
        self.assertFalse(self.calls)

    def test_missing_owner_source_freeze_is_not_synthesized(self):
        self.freeze["sources"] = [row for row in self.freeze["sources"]
                                  if row["logical_path"] != "scripts/ci_owned_process.py"]
        self.rebind_fixture()
        with self.assertRaisesRegex(RuntimeError, "Missing explicit frozen role"):
            capture.capture(self.arguments())
        self.assertFalse(self.calls)

    def test_changed_source_refuses_before_any_launch(self):
        (self.root / "scripts/ci_mac_process.py").write_bytes(b"CHANGED SYNTHETIC SOURCE\n")
        with self.assertRaisesRegex(RuntimeError, "Pinned identity differs"):
            capture.capture(self.arguments())
        self.assertFalse(self.calls)

    def test_installed_binary_must_match_bound_build_bytes(self):
        module = self.bindings["installed_modules"]["imagejxx._native"]
        self.bindings["installed_modules"]["imagejxx._native"] = put(Path(module["path"]), b"OTHER SYNTHETIC BUILD\n")
        save(self.bindings_path, self.bindings)
        with self.assertRaisesRegex(RuntimeError, "Installed compiled product differs"):
            capture.capture(self.arguments())
        self.assertFalse(self.calls)

    def test_failed_or_unresolved_owner_cannot_publish_provenance(self):
        for delta in ({"exit_code": 3}, {"cleanup_complete": False}, {"target_completed": False},
                      {"state": "running"}, {"timed_out": True}, {"target_pid": True}):
            with self.subTest(delta=delta):
                args = self.arguments()
                self.change_terminal = lambda row, change=delta: row.update(change)
                with self.assertRaisesRegex(RuntimeError, "successful resolved owned exit"):
                    capture.capture(args)
                result = capture.read_json(args.output / "capture.json")
                self.assertEqual(result["state"], "failed")
                self.assertFalse(result["cases"])
                self.assertTrue(Path(result["processes"][0]["owned_process_receipt"]["path"]).is_file())
                self.assertFalse((args.output / "provenance.json").exists())

    def test_returned_owner_result_must_equal_durable_receipt(self):
        def change_returned(_raw, terminal):
            terminal["target_pid"] += 1
        self.after_case = change_returned
        with self.assertRaisesRegex(RuntimeError, "Owned terminal receipt differs"):
            capture.capture(self.arguments())

    def test_raw_envelope_and_limits_remain_required(self):
        for delta in ({"frames": [{}] * 181}, {"elapsed_seconds": 15.001},
                      {"acceptance_claim": True}, {"context": "other"}, {"case_id": "HU05"}):
            with self.subTest(delta=list(delta)):
                self.change_raw = lambda row, change=delta: row.update(change)
                with self.assertRaisesRegex(RuntimeError, "Actual producer envelope/caps differ"):
                    capture.capture(self.arguments())

    def test_raw_report_size_cap_precedes_json_decode(self):
        def oversized(raw, _terminal):
            with raw.open("wb") as stream:
                stream.truncate(16 * capture.MIB + 1)
        self.after_case = oversized
        with self.assertRaisesRegex(RuntimeError, "JSON byte bound"):
            capture.capture(self.arguments())

    def test_paired_cap_counts_every_pinned_report_without_rewriting_it(self):
        real_pin = capture.pin
        def declared_large(path, allow_empty=False):
            value = real_pin(path, allow_empty)
            if Path(path).name == "report.json":
                value = {**value, "bytes": 5 * capture.MIB}
            return value
        # Metadata-only arithmetic control: actual raw files remain tiny.
        with mock.patch.object(capture, "pin", side_effect=declared_large):
            with self.assertRaisesRegex(RuntimeError, "Paired raw/origin byte bound"):
                capture.capture(self.arguments())
        self.assertEqual(len(self.calls), 7)

    def test_mutated_product_after_capture_refuses_final_provenance(self):
        def mutate_after_eighth(_raw, _terminal):
            if len(self.calls) == 8:
                Path(self.bindings["installed_modules"]["imagejxx._native"]["path"]).write_bytes(b"MUTATED\n")
        self.after_case = mutate_after_eighth
        args = self.arguments()
        with self.assertRaisesRegex(RuntimeError, "Pinned identity differs"):
            capture.capture(args)
        self.assertEqual(len(self.calls), 8)
        self.assertFalse((args.output / "provenance.json").exists())

    def test_fresh_workspace_output_accepts_tmp_and_refuses_existing_or_outside(self):
        args = self.arguments()
        self.assertEqual(capture.history_output(args.output), args.output)
        args.output.mkdir()
        for path in (args.output, self.root, self.root.parent / "foreign-history-capture"):
            with self.subTest(path=path):
                with self.assertRaisesRegex(RuntimeError, "owned workspace child"):
                    capture.history_output(path)

    def test_output_link_is_refused_without_traversal(self):
        alias = self.root / "linked"
        try:
            alias.symlink_to(self.root / ".tmp", target_is_directory=True)
        except OSError as error:
            self.skipTest("Host does not permit a real temporary directory symlink: " + str(error))
        with self.assertRaisesRegex(RuntimeError, "linked/reparse ancestor"):
            capture.history_output(alias / "new")

    def test_other_kinds_keep_original_windows_boundary(self):
        args = self.arguments()
        args.kind = "shortcuts"
        with mock.patch.object(capture, "os", SimpleNamespace(name="posix")):
            with self.assertRaisesRegex(RuntimeError, "Windows-only installed capture"):
                capture.capture(args)
        self.assertFalse(args.output.exists())


class HistoryOwnershipControls(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory(prefix="history-owner-control-")
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name).resolve(strict=True)
        self.report = {"processes": [], "limits": {"stage_bytes": 1024 * 1024}}

    def test_live_scan_survives_atomic_receipt_rename_and_final_scan_counts_it(self):
        temporary = self.root / "case.process.json.writing"
        terminal = self.root / "case.process.json"
        temporary.write_bytes(b"receipt bytes")
        original_stat = os.stat
        renamed = []
        def rename_before_stat(path, **options):
            if Path(path) == temporary and not renamed:
                os.replace(temporary, terminal)
                renamed.append(True)
            return original_stat(path, **options)
        with mock.patch.object(capture.os, "stat", side_effect=rename_before_stat):
            capture.storage(self.root, live=True)
        self.assertEqual(renamed, [True])
        self.assertEqual(capture.storage(self.root),
                         {"bytes": 13, "files": 1, "log_bytes": 0})
        with self.assertRaisesRegex(RuntimeError, "byte/file/log bounds"):
            capture.storage(self.root, {"stage_bytes": 12})

    def test_only_live_missing_entry_is_tolerated(self):
        entry = mock.Mock(path=str(self.root / "vanished"))
        entry.stat.side_effect = FileNotFoundError("synthetic vanished entry")
        scan = mock.MagicMock()
        scan.__enter__.return_value = [entry]
        with mock.patch.object(capture.os, "scandir", return_value=scan), \
                mock.patch.object(capture.os, "stat", side_effect=FileNotFoundError("synthetic vanished entry")):
            self.assertEqual(capture.storage(self.root, live=True)["bytes"], 0)
            with self.assertRaises(FileNotFoundError):
                capture.storage(self.root)
        with mock.patch.object(capture.os, "scandir", return_value=scan), \
                mock.patch.object(capture.os, "stat", side_effect=PermissionError("synthetic denied entry")):
            with self.assertRaises(PermissionError):
                capture.storage(self.root, live=True)

    def test_live_violation_interrupts_owner_but_never_accepts_its_success(self):
        noticed = threading.Event()
        real_storage = capture.storage
        def observed_storage(*args, **kwargs):
            if threading.current_thread().name == "history-storage-monitor":
                raise RuntimeError("synthetic live storage violation")
            return real_storage(*args, **kwargs)
        def owner(_argv, **options):
            self.assertTrue(noticed.wait(2), "Monitor did not observe the owned scratch")
            terminal = {"schema": "imagejxx.owned-command.001", "state": "exited", "exit_code": 0,
                        "target_completed": True, "cleanup_complete": True, "target_pid": 123,
                        "timed_out": False, "fixture": "synthetic-transport-only"}
            save(options["receipt_path"], terminal)
            return terminal
        with mock.patch.object(capture, "storage", side_effect=observed_storage), \
                mock.patch.object(capture._thread, "interrupt_main", side_effect=noticed.set) as interrupt:
            with self.assertRaisesRegex(RuntimeError, "Live history storage guard refused"):
                capture.run_history_owned(owner, ["synthetic"], self.root, "case", time.monotonic() + 5, self.report)
        interrupt.assert_called_once_with()
        row = self.report["processes"][0]
        self.assertEqual(row["state"], "failed")
        self.assertEqual(row["live_storage_monitor"]["state"], "failed")
        self.assertEqual(row["live_storage_monitor"]["first_violation"]["message"], "synthetic live storage violation")
        self.assertTrue(Path(row["owned_process_receipt"]["path"]).is_file())
        self.assertFalse(any(thread.name == "history-storage-monitor" for thread in threading.enumerate()))

    def test_owner_error_preserves_cleanup_receipt_and_joins_monitor(self):
        def owner(_argv, **options):
            save(options["receipt_path"], {"schema": "imagejxx.owned-command.001", "state": "ownership_failed",
                                          "cleanup_complete": False, "fixture": "synthetic-transport-only"})
            raise RuntimeError("synthetic unresolved ownership")
        with self.assertRaisesRegex(RuntimeError, "synthetic unresolved ownership"):
            capture.run_history_owned(owner, ["synthetic"], self.root, "case", time.monotonic() + 5, self.report)
        row = self.report["processes"][0]
        self.assertEqual(row["state"], "failed")
        self.assertEqual(row["live_storage_monitor"]["state"], "stopped")
        terminal = capture.read_json(Path(row["owned_process_receipt"]["path"]))
        self.assertIs(terminal["cleanup_complete"], False)
        self.assertFalse(any(thread.name == "history-storage-monitor" for thread in threading.enumerate()))

    def test_expired_case_deadline_never_calls_owner(self):
        owner = mock.Mock()
        with self.assertRaisesRegex(RuntimeError, "deadline exhausted"):
            capture.run_history_owned(owner, ["synthetic"], self.root, "case", time.monotonic() - 1, self.report)
        owner.assert_not_called()

    def test_history_uses_only_platform_owner_and_original_other_memberships(self):
        owner, mac_owner = mock.Mock(), mock.Mock()
        module = SimpleNamespace(run_owned=owner, run_mac_owned=mac_owner)
        loader = SimpleNamespace(exec_module=mock.Mock())
        for platform, name, expected in (("win32", "ci_owned_process", owner),
                                         ("linux", "ci_owned_process", owner),
                                         ("darwin", "ci_mac_process", mac_owner)):
            with self.subTest(platform=platform), mock.patch.object(capture.sys, "platform", platform), \
                    mock.patch.object(capture.importlib.util, "spec_from_file_location",
                                      return_value=SimpleNamespace(loader=loader)) as spec, \
                    mock.patch.object(capture.importlib.util, "module_from_spec", return_value=module):
                self.assertIs(capture.history_owner(), expected)
                self.assertEqual(Path(spec.call_args.args[1]).name, name + ".py")
        self.assertEqual(capture.SPEC["shortcuts"]["cases"], tuple(f"K{i:02}" for i in range(1, 11)))
        self.assertEqual(capture.SPEC["shortcuts-supplement"]["cases"], tuple(f"K{i:02}" for i in range(11, 15)))
        self.assertEqual(capture.SPEC["scientific-orientation"]["cases"], tuple(f"O{i:02}" for i in range(1, 13)))
        self.assertEqual(capture.COMMON["process_helper"], "scripts/capture_windows_platform_workload.py")

    def test_linux_engine_uses_kernel_executable_path(self):
        executable = self.root / "python"
        executable.write_bytes(b"SYNTHETIC ENGINE\n")
        with mock.patch.object(capture, "os", SimpleNamespace(name="posix")), \
                mock.patch.object(capture.sys, "platform", "linux"), \
                mock.patch.object(capture, "Path", return_value=executable) as path:
            self.assertEqual(capture.engine(portable=True), executable)
        path.assert_called_once_with("/proc/self/exe")

    def test_mac_engine_uses_loader_path_and_refuses_unavailable_identity(self):
        executable = self.root / "python"
        executable.write_bytes(b"SYNTHETIC ENGINE\n")
        def identify(buffer, _size):
            buffer.value = os.fsencode(executable)
            return 0
        with mock.patch.object(capture, "os", SimpleNamespace(name="posix", fsdecode=os.fsdecode)), \
                mock.patch.object(capture.sys, "platform", "darwin"), \
                mock.patch.object(capture.ctypes, "CDLL", return_value=SimpleNamespace(_NSGetExecutablePath=identify)):
            self.assertEqual(capture.engine(portable=True), executable)
        def unavailable(_buffer, _size):
            return -1
        with mock.patch.object(capture, "os", SimpleNamespace(name="posix", fsdecode=os.fsdecode)), \
                mock.patch.object(capture.sys, "platform", "darwin"), \
                mock.patch.object(capture.ctypes, "CDLL", return_value=SimpleNamespace(_NSGetExecutablePath=unavailable)):
            with self.assertRaisesRegex(RuntimeError, "actual CPython executable"):
                capture.engine(portable=True)


if __name__ == "__main__":
    unittest.main()
