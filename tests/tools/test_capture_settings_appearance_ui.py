"""Metadata-only C2 paired-capture controls. Synthetic products never execute."""
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
p = ROOT / "tests/tools/test_capture_shortcuts_history_ui.py"
s = importlib.util.spec_from_file_location("appearance_history_fixture", p)
base = importlib.util.module_from_spec(s)
exec(compile(p.read_bytes(), str(p), "exec", dont_inherit=True), base.__dict__)
capture = base.capture


class AppearanceTransport(unittest.TestCase):
    def setUp(self):
        self.f = base.InstalledHistoryTransport()
        self.f.setUp()
        self.addCleanup(self.f.doCleanups)
        f = self.f
        roles = capture.COMMON | capture.SPEC["appearance"]["roles"]
        f.freeze["sources"] = [{"logical_path": n, **base.put(f.root/n, ("SYNTHETIC SOURCE ONLY: "+n+"\n").encode())}
                               for n in sorted(set(roles.values()))]
        f.bindings.update(kind="c2.installed-ui-capture-bindings.001", kind_selection="appearance")
        f.rebind_fixture()
        self.owner = f.stack.enter_context(mock.patch.object(capture, "history_owner", return_value=self.fake_owner))

    def arguments(self):
        args = self.f.arguments()
        args.kind = "appearance"
        return args

    def fake_owner(self, argv, **options):
        f = self.f
        context = "imgui_cpp" if argv[0] == str(f.app) else "imgui_python"
        if context == "imgui_cpp":
            case, directory = argv[-2], Path(argv[-1])
        else:
            case = argv[argv.index("--python-case")+1]
            directory = Path(argv[argv.index("--directory")+1])
        f.calls.append((case, context, argv, options))
        preferences = directory.parent / "preferences.json"
        self.assertEqual(preferences.exists(), case == "appearance-reopen")
        if case == "appearance-persist":
            preferences.write_bytes(b'{"synthetic":true}\n')
        self.assertEqual(preferences.read_bytes(), b'{"synthetic":true}\n')
        directory.mkdir()
        runtime = {"process_id": 10000+len(f.calls), **f.bindings["products"][context]}
        raw = {"schema": "c2.settings-appearance.ui.001", "case_id": case, "context": context,
               "state": "observed", "acceptance_claim": False, "frames": [{}]*750, "elapsed_seconds": 60,
               "runtime_before": runtime, "runtime_after": runtime, "synthetic_transport_fixture": True}
        if f.change_raw:
            f.change_raw(raw)
        base.save(directory / "case.json", raw)
        if context == "imgui_python":
            base.save(directory.parent / (case+"-launcher-origin.json"), {
                "before": f.bindings["products"][context], "after": f.bindings["products"][context],
                "process_id": runtime["process_id"], "modules_before": f.bindings["installed_modules"],
                "modules_after": f.bindings["installed_modules"]})
        terminal = {"schema": "imagejxx.owned-command.001", "state": "exited", "exit_code": 0,
                    "target_pid": runtime["process_id"], "target_completed": True, "cleanup_complete": True, "timed_out": False}
        base.save(options["receipt_path"], terminal)
        return terminal

    def test_four_fresh_processes_share_preferences_only_with_same_launcher(self):
        args = self.arguments()
        capture.capture(args)
        self.assertEqual([(c, x) for c, x, *_ in self.f.calls], [(c, x) for c in capture.SPEC["appearance"]["cases"] for x in capture.CONTEXTS])
        report = capture.read_json(args.output/"capture.json")
        self.assertEqual(report["state"], "passed")
        self.assertFalse(report["acceptance_claim"])
        self.assertEqual(len(report["cases"]), 4)
        for case, context, argv, options in self.f.calls:
            self.assertLessEqual(options["timeout"], 60)
            self.assertEqual((args.output/context/case/"case.json").is_file(), True)
        self.assertNotEqual((args.output/"imgui_cpp/preferences.json").parent,
                            (args.output/"imgui_python/preferences.json").parent)

    def test_report_frame_and_time_overrun_and_failed_state_refuse(self):
        for change in (lambda r: r.update(frames=[{}]*751), lambda r: r.update(elapsed_seconds=60.1),
                       lambda r: r.update(state="failed")):
            self.f.change_raw = change
            with self.assertRaisesRegex(RuntimeError, "producer envelope"):
                capture.capture(self.arguments())

    def test_required_schema_and_actual_runtime_identity_refuse(self):
        for change in (lambda r: r.update(schema="imagejxx.history-settings-ui-observations"),
                       lambda r: r.update(runtime_after=dict(r["runtime_before"], process_id=1))):
            self.f.change_raw = change
            with self.assertRaises(RuntimeError):
                capture.capture(self.arguments())


if __name__ == "__main__":
    unittest.main()
