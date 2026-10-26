"""Synthetic C3 transport boundaries; no image processor or UI is executed."""
import importlib.util
import json
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
p = ROOT / "tests/tools/test_capture_shortcuts_history_ui.py"
spec = importlib.util.spec_from_file_location("drawing_history_fixture", p)
base = importlib.util.module_from_spec(spec)
exec(compile(p.read_bytes(), str(p), "exec", dont_inherit=True), base.__dict__)
capture = base.capture
CASES = ("drawing-persist", "drawing-reopen")
CONTEXTS = ("imgui_cpp", "imgui_python")
SEED = {"format": "imagejxx-preferences", "version": 1,
        "values": {"c3_future_value": {"raw": [1, "two", True]}},
        "c3_future_root": {"opaque": ["retain", 17]}, "extras": {"c3": "retain extra"}}


class DrawingTransport(unittest.TestCase):
    def setUp(self):
        self.f = base.InstalledHistoryTransport()
        self.f.setUp()
        self.addCleanup(self.f.doCleanups)
        f = self.f
        roles = capture.COMMON | capture.SPEC["drawing"]["roles"]
        f.freeze["sources"] = [
            {"logical_path": n, **base.put(f.root / n, ("SYNTHETIC SOURCE ONLY: " + n + "\n").encode())}
            for n in sorted(set(roles.values()))]
        f.bindings.update(kind="c3.installed-ui-capture-bindings.001", kind_selection="drawing")
        f.rebind_fixture()
        f.stack.enter_context(mock.patch.object(capture, "history_owner", return_value=self.owner))

    def arguments(self):
        args = self.f.arguments()
        args.kind = "drawing"
        return args

    def owner(self, argv, **options):
        f = self.f
        context = "imgui_cpp" if argv[0] == str(f.app) else "imgui_python"
        if context == "imgui_cpp":
            self.assertEqual(len(argv), 5)
            self.assertEqual(argv[1], "--settings-drawing-ui-case")
            case, directory = argv[2], Path(argv[3])
            self.assertEqual(Path(argv[4]), directory.parent / "preferences.json")
        else:
            case = argv[argv.index("--python-case") + 1]
            directory = Path(argv[argv.index("--directory") + 1])
            self.assertEqual(argv[argv.index("--kind") + 1], "drawing")
        f.calls.append((case, context, argv, options))
        preferences = directory.parent / "preferences.json"
        self.assertTrue(preferences.is_file())
        value = json.loads(preferences.read_bytes())
        if case == "drawing-persist":
            # This is the actual parent-written seed before our fake host starts.
            self.assertEqual(value, SEED)
            value["values"]["synthetic_saved_value"] = context
            preferences.write_text(json.dumps(value) + "\n", encoding="utf8")
        else:
            self.assertEqual(value["values"]["synthetic_saved_value"], context)
            value["values"].pop("synthetic_saved_value")
            self.assertEqual(value, SEED)
        directory.mkdir()
        runtime = {"process_id": 10000 + len(f.calls), **f.bindings["products"][context]}
        raw = {"schema": "c3.settings-drawing.ui.001", "case_id": case, "context": context,
               "state": "observed", "acceptance_claim": False, "frames": [{}] * 750,
               "elapsed_seconds": 60, "runtime_before": runtime, "runtime_after": runtime,
               "synthetic_transport_fixture": True}
        if f.change_raw:
            f.change_raw(raw)
        base.save(directory / "case.json", raw)
        if context == "imgui_python":
            base.save(directory.parent / (case + "-launcher-origin.json"), {
                "before": f.bindings["products"][context], "after": f.bindings["products"][context],
                "process_id": runtime["process_id"], "modules_before": f.bindings["installed_modules"],
                "modules_after": f.bindings["installed_modules"]})
        proof = {"schema": "imagejxx.owned-command.001", "state": "exited", "exit_code": 0,
                 "target_pid": runtime["process_id"], "target_completed": True,
                 "cleanup_complete": True, "timed_out": False}
        base.save(options["receipt_path"], proof)
        return proof

    def test_four_exact_routes_seed_before_launch_and_share_only_own_context(self):
        args = self.arguments()
        capture.capture(args)
        self.assertEqual([(case, context) for case, context, *_ in self.f.calls],
                         [(case, context) for case in CASES for context in CONTEXTS])
        report = capture.read_json(args.output / "capture.json")
        self.assertEqual((report["state"], report["kind_selection"], report["acceptance_claim"]),
                         ("passed", "drawing", False))
        self.assertEqual([(row["id"], row["context"]) for row in report["cases"]],
                         [(case, context) for case in CASES for context in CONTEXTS])
        for _, _, _, options in self.f.calls:
            self.assertLessEqual(options["timeout"], 60)
        self.assertNotEqual((args.output / "imgui_cpp/preferences.json").read_bytes(),
                            (args.output / "imgui_python/preferences.json").read_bytes())

    def test_frame_time_state_schema_and_runtime_failures_stay_fatal(self):
        for change in (lambda row: row.update(frames=[{}] * 751),
                       lambda row: row.update(elapsed_seconds=60.1),
                       lambda row: row.update(state="failed"),
                       lambda row: row.update(schema="c2.settings-appearance.ui.001"),
                       lambda row: row.update(runtime_after=dict(row["runtime_before"], process_id=1))):
            self.f.change_raw = change
            with self.assertRaises(RuntimeError):
                capture.capture(self.arguments())

    def test_missing_independent_rounded_role_refuses_before_any_fake_host(self):
        self.f.freeze["sources"] = [row for row in self.f.freeze["sources"]
                                    if row["logical_path"] != "tests/native/rounded_path_reference.inc"]
        self.f.rebind_fixture()
        with self.assertRaises(RuntimeError):
            capture.capture(self.arguments())
        self.assertEqual(self.f.calls, [])


if __name__ == "__main__":
    unittest.main()
