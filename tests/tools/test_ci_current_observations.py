"""Mocked current-capture transport; never launches an installed app or compiler.

Fake pixel members deliberately contain one byte, not qualified image data.
Successful transport therefore cannot be mistaken for independent RGB/Gaussian
acceptance. All scratch is small, exclusive temporary metadata and removed.
"""
from contextlib import redirect_stderr
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import Mock, patch
import urllib.request


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "ci_current_observations_under_test", ROOT / "scripts/ci_current_observations.py")
current = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(current)
R16 = "P02a.shared_ui.20.busy-failure-cancel-atomicity.held-preview-lease-lowered-budget"
H1 = [f"H1-{number:02d}" for number in range(1, 13)]
FIELDS = ("axis", "origin", "first_channels", "second_channels", "automatic")
ROWS = [dict(zip(FIELDS, values), roi=8)
        for axis in range(3) for origin in range(2) for first in range(1, 8)
        for second in range(1, 8) if first != second for automatic in range(2)
        for values in [(axis, origin, first, second, automatic)]]


class OwnerFailure(RuntimeError):
    def __init__(self, receipt):
        super().__init__("synthetic owned-command timeout")
        self.receipt = receipt


class CaptureTransport(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx-current-metadata-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.output = self.root / "observations"
        self.app = self.write("installed/imagejxx", b"synthetic application; not executable")
        self.rgb = self.write("build/wheel/synthetic/imagejxx_rgb_combined_input_tests", b"synthetic native test; not executable")
        for name in current.SOURCE_INPUTS:
            self.write(name, b"synthetic source-identity member\n")
        self.write_json(current.RGB_REFERENCE, {"cases": ROWS})
        self.environment = {"PATH": "explicit synthetic tool path", "SECRET": "never-serialize-this-value"}
        self.calls = []
        self.after_emit = None
        self.exit_code = 0
        self.cleanup_complete = True
        self.owner_exception = False
        self.start_patch(patch.object(subprocess, "Popen", side_effect=AssertionError("No actual subprocess in transport controls")))
        self.start_patch(patch.object(urllib.request, "urlopen", side_effect=AssertionError("No network in transport controls")))
        self.start_patch(patch.object(sys, "dont_write_bytecode", True))

    def start_patch(self, value):
        result = value.start()
        self.addCleanup(value.stop)
        return result

    def write(self, relative, raw):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
        return path

    def write_json(self, relative, value):
        return self.write(relative, (json.dumps(value) + "\n").encode("utf-8"))

    def emit_rgb(self, destination):
        rows = []
        for index, source in enumerate(ROWS):
            member = str(index) + "-applied.bin"
            (destination / member).write_bytes(b"x")
            rows.append({**{name: source[name] for name in FIELDS}, "pixels_file": member,
                         "not_an_acceptance_oracle": True})
        return {"state": "passed", "cases": 504, "results": rows}, "results.json"

    def emit_h1(self, destination):
        return {"schema_version": 1, "context": "imgui_cpp", "status": "passed", "acceptance_claim": False,
                "cases": [{"case_id": case, "context": "imgui_cpp"} for case in H1]}, "imgui-cpp.json"

    def emit_r16(self, destination):
        case = destination / "R16"
        case.mkdir()
        preferences = case / "preferences.json"
        preferences.write_bytes(b'{"opaque":"original preferences bytes"}\n')
        return {"schema_version": 1, "context": "imgui_cpp", "status": "passed", "acceptance_claim": False,
                "case_id": R16, "cases": [{"case_id": R16, "context": "imgui_cpp",
                "preferences_file": {"path": str(preferences)}}]}, "imgui-cpp.json"

    def execute(self, argv, **options):
        self.calls.append((list(argv), options))
        destination = Path(argv[1] if argv[0] == str(self.rgb) else argv[-1])
        self.assertFalse(destination.exists(), "Producer requires a fresh original directory")
        destination.mkdir()
        kind = destination.name
        value, name = {"rgb-combined": self.emit_rgb, "gaussian-cancellation": self.emit_h1,
                       "gaussian-resource": self.emit_r16}[kind](destination)
        if self.after_emit:
            self.after_emit(kind, destination, value)
        raw = (json.dumps(value, indent=2) + "\n").encode("utf-8")
        (destination / name).write_bytes(raw)
        options["stdout"].write(b"synthetic producer log; no real capture\n")
        process = {"state": "exited", "target_completed": True,
                   "cleanup_complete": self.cleanup_complete, "exit_code": self.exit_code,
                   "descendants_remained_after_target_completion": True}
        if self.owner_exception:
            process.update(state="timed_out", exit_code=None)
        Path(options["receipt_path"]).write_text(json.dumps(process), encoding="utf-8")
        if self.owner_exception:
            raise OwnerFailure(process)
        return process

    def capture(self, **changes):
        arguments = dict(app=self.app, rgb_executable=self.rgb, output=self.output,
                         environment=self.environment, root=self.root)
        arguments.update(changes)
        with patch.object(current, "load_owner", return_value=self.execute):
            return current.capture_current(**arguments)

    def receipt(self):
        return json.loads((self.output / "receipt.json").read_bytes())

    def test_three_serial_original_producers_and_reader_paths_are_exact(self):
        result = self.capture()
        self.assertEqual(result, self.receipt())
        self.assertEqual(result["state"], "captured")
        self.assertIs(result["acceptance_claim"], False)
        self.assertIs(result["historical_gaussian_reference_prepared"], False)
        self.assertEqual(result["inputs_before"], result["inputs_after"])
        self.assertEqual([argv for argv, _ in self.calls], [
            [str(self.rgb), str(self.output / "rgb-combined"), "504"],
            [str(self.app), "--gaussian-cancellation-consumer-test", "--consumer-dir", str(self.output / "gaussian-cancellation")],
            [str(self.app), "--gaussian-resource-consumer-test", "--consumer-dir", str(self.output / "gaussian-resource")],
        ])
        expected = {"IMAGEJXX_RGB_COMBINED_INPUT_REPORT": str(self.output / "rgb-combined/results.json"),
                    "IMAGEJXX_H1_CPP_REPORT": str(self.output / "gaussian-cancellation/imgui-cpp.json"),
                    "IMAGEJXX_R16_CPP_REPORT": str(self.output / "gaussian-resource/imgui-cpp.json")}
        self.assertEqual(result["reader_environment"], expected)
        self.assertEqual([options["timeout"] for _, options in self.calls], [1200, 180, 180])
        self.assertEqual([row["observation"]["cases"] for row in result["commands"]], [504, 12, 1])
        for (_, options), command in zip(self.calls, result["commands"]):
            self.assertEqual(options["cwd"], self.root)
            self.assertEqual(options["env"], {**self.environment, "PYTHONDONTWRITEBYTECODE": "1"})
            self.assertEqual(options["stderr"], subprocess.STDOUT)
            self.assertIs(command["cleanup_complete"], True)
            self.assertEqual(command["state"], "captured")
            report = Path(command["report"]["path"])
            raw = report.read_bytes()
            self.assertEqual((command["report"]["bytes"], command["report"]["sha256"]),
                             (len(raw), hashlib.sha256(raw).hexdigest()))
        self.assertEqual(self.environment, {"PATH": "explicit synthetic tool path", "SECRET": "never-serialize-this-value"})
        self.assertNotIn("never-serialize-this-value", (self.output / "receipt.json").read_text())
        # Deliberately invalid image payloads are transported unchanged. Only
        # the original independent reader can grant numerical/UI acceptance.
        self.assertEqual((self.output / "rgb-combined/0-applied.bin").read_bytes(), b"x")
        r16 = json.loads(Path(expected["IMAGEJXX_R16_CPP_REPORT"]).read_bytes())
        preferences = Path(r16["cases"][0]["preferences_file"]["path"])
        self.assertEqual(preferences, self.output / "gaussian-resource/R16/preferences.json")
        self.assertEqual(preferences.read_bytes(), b'{"opaque":"original preferences bytes"}\n')
        self.assertFalse((self.output / "reports").exists(), "No report relocation/rewriting is allowed")

    def test_nonzero_producer_retains_failure_bytes_and_no_reader_environment(self):
        self.exit_code = 7
        with self.assertRaisesRegex(ValueError, "successful resolved owned exit"):
            self.capture()
        result = self.receipt()
        self.assertEqual(result["state"], "failed")
        self.assertEqual(result["reader_environment"], {})
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(result["commands"][0]["exit_code"], 7)
        self.assertTrue((self.output / "rgb-combined/results.json").is_file())
        self.assertTrue((self.output / "rgb-combined.process.json").is_file())
        self.assertFalse((self.output / "rgb-combined.files.json").exists())

    def test_unresolved_cleanup_never_reads_capture_reports(self):
        self.cleanup_complete = False
        with patch.object(current, "envelope", side_effect=AssertionError("Read before cleanup")) as read:
            with self.assertRaisesRegex(ValueError, "resolved owned exit"):
                self.capture()
            read.assert_not_called()
        result = self.receipt()
        self.assertIs(result["commands"][0]["cleanup_complete"], False)
        self.assertEqual(result["reader_environment"], {})

    def test_owner_exception_preserves_its_terminal_receipt_and_partial_capture(self):
        self.owner_exception = True
        with self.assertRaises(OwnerFailure):
            self.capture()
        result = self.receipt()
        self.assertEqual(result["commands"][0]["process_state"], "timed_out")
        self.assertIs(result["commands"][0]["cleanup_complete"], True)
        self.assertEqual(result["reader_environment"], {})
        self.assertEqual(json.loads((self.output / "rgb-combined.process.json").read_bytes())["state"], "timed_out")
        self.assertTrue((self.output / "rgb-combined/results.json").exists())

    def test_rgb_missing_rows_refuse_without_gaussian_commands(self):
        self.after_emit = lambda kind, directory, row: row["results"].pop() if kind == "rgb-combined" else None
        with self.assertRaisesRegex(ValueError, "all 504"):
            self.capture()
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(self.receipt()["reader_environment"], {})

    def test_same_count_rgb_duplicate_or_substituted_id_refuses(self):
        destination = self.root / "direct-rgb"
        destination.mkdir()
        row, name = self.emit_rgb(destination)
        expected = {tuple(item[key] for key in FIELDS) for item in ROWS}
        for change in (lambda value: value["results"].__setitem__(1, dict(value["results"][0])),
                       lambda value: value["results"][0].__setitem__("axis", 17)):
            with self.subTest(change=change):
                candidate = json.loads(json.dumps(row))
                change(candidate)
                (destination / name).write_text(json.dumps(candidate), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "identities differ"):
                    current.envelope("rgb-combined", destination / name, expected)

    def test_rgb_asset_traversal_and_aliases_refuse(self):
        destination = self.root / "direct-rgb"
        destination.mkdir()
        row, name = self.emit_rgb(destination)
        expected = {tuple(item[key] for key in FIELDS) for item in ROWS}
        for member, message in (("../0-applied.bin", "plain relative"), ("C:0-applied.bin", "plain relative"),
                                ("1-applied.bin", "reused a pixel")):
            with self.subTest(member=member):
                candidate = json.loads(json.dumps(row))
                candidate["results"][0]["pixels_file"] = member
                (destination / name).write_text(json.dumps(candidate), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, message):
                    current.envelope("rgb-combined", destination / name, expected)

    def test_gaussian_missing_foreign_and_duplicate_case_ids_refuse(self):
        destination = self.root / "h1"
        destination.mkdir()
        row, name = self.emit_h1(destination)
        for kind in ("missing", "duplicate", "foreign-context"):
            with self.subTest(kind=kind):
                candidate = json.loads(json.dumps(row))
                if kind == "missing":
                    candidate["cases"].pop()
                elif kind == "duplicate":
                    candidate["cases"][1] = dict(candidate["cases"][0])
                else:
                    candidate["cases"][0]["context"] = "imgui_python"
                (destination / name).write_text(json.dumps(candidate), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "case identities"):
                    current.envelope("gaussian-cancellation", destination / name, set())

    def test_resource_acceptance_claim_or_wrong_root_identity_refuses(self):
        destination = self.root / "r16"
        destination.mkdir()
        row, name = self.emit_r16(destination)
        for changes, message in (({"acceptance_claim": True}, "capture envelope"),
                                  ({"case_id": "different-case"}, "root case identity")):
            with self.subTest(changes=changes):
                (destination / name).write_text(json.dumps({**row, **changes}), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, message):
                    current.envelope("gaussian-resource", destination / name, set())

    def test_input_change_after_first_route_refuses_later_launch(self):
        def mutate(kind, directory, row):
            if kind == "rgb-combined":
                self.rgb.write_bytes(b"modified binary after producer return")
        self.after_emit = mutate
        with self.assertRaisesRegex(ValueError, "input changed before launch"):
            self.capture()
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(self.receipt()["reader_environment"], {})
        self.assertTrue((self.output / "rgb-combined/results.json").exists())

    def test_last_route_source_change_refuses_environment_publication(self):
        def mutate(kind, directory, row):
            if kind == "gaussian-resource":
                (self.root / "tests/python/test_gaussian_resource_ui.py").write_bytes(b"changed original reader")
        self.after_emit = mutate
        with self.assertRaisesRegex(ValueError, "input changed during capture"):
            self.capture()
        result = self.receipt()
        self.assertEqual(len(self.calls), 3)
        self.assertNotEqual(result["inputs_before"], result["inputs_after"])
        self.assertEqual(result["reader_environment"], {})

    def test_missing_input_or_existing_output_refuses_without_commands(self):
        self.output.mkdir()
        (self.output / "keep").write_bytes(b"unrelated previous output")
        with self.assertRaisesRegex(ValueError, "fresh owned"):
            self.capture()
        self.assertEqual((self.output / "keep").read_bytes(), b"unrelated previous output")
        self.assertEqual(self.calls, [])
        with self.assertRaises(FileNotFoundError):
            self.capture(app=self.root / "missing-app", output=self.root / "fresh")
        self.assertFalse((self.root / "fresh").exists())

    def test_outside_root_invalid_environment_and_deadlines_refuse_without_outputs(self):
        for changes, message in (({"output": self.root.parent / (self.root.name + "-outside")}, "fresh owned"),
                                 ({"output": self.root}, "fresh owned"),
                                 ({"environment": {"KEY": 17}}, "string-to-string"),
                                 ({"environment": {"BAD=KEY": "value"}}, "string-to-string")):
            with self.subTest(changes=changes), self.assertRaisesRegex(ValueError, message):
                self.capture(**changes)
        for name, values in (("rgb_timeout", (0, -1, True, float("nan"), float("inf"), 1201)),
                             ("gaussian_timeout", (0, -1, True, float("nan"), 181))):
            for value in values:
                with self.subTest(name=name, value=value), self.assertRaisesRegex(ValueError, "fixed deadline"):
                    self.capture(**{name: value})
        self.assertFalse(self.output.exists())
        self.assertEqual(self.calls, [])

    def test_json_duplicate_nonfinite_and_size_bounds_refuse(self):
        path = self.write("report.json", b'{"same":1,"same":2}')
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            current.read_report(path)
        for raw in (b'{"value":NaN}', b'{"value":1e999}'):
            path.write_bytes(raw)
            with self.assertRaisesRegex(ValueError, "Nonfinite"):
                current.read_report(path)
        path.write_bytes(b'{"value":1}')
        with patch.object(current, "MAX_REPORT_BYTES", 3):
            with self.assertRaisesRegex(ValueError, "byte bound"):
                current.read_report(path)

    def test_inventory_limits_preserve_all_current_bytes(self):
        directory = self.root / "capture"
        self.write("capture/first", b"first")
        self.write("capture/second", b"second")
        for key, maximum in (("MAX_FILES", 1), ("MAX_CAPTURE_BYTES", 10)):
            with self.subTest(key=key), patch.object(current, key, maximum):
                with self.assertRaisesRegex(ValueError, "inventory bound"):
                    current.inventory(directory)
        self.assertEqual((directory / "first").read_bytes(), b"first")
        self.assertEqual((directory / "second").read_bytes(), b"second")

    def test_real_linked_ancestor_refuses_before_touching_external_members(self):
        external = self.root / "external"
        external.mkdir()
        self.write("external/keep", b"untouched external bytes")
        link = self.root / "linked"
        try:
            link.symlink_to(external, target_is_directory=True)
        except (OSError, NotImplementedError) as error:
            self.skipTest("Host cannot create bounded test symlink: " + str(error))
        with self.assertRaisesRegex(ValueError, "Linked observation path/ancestor"):
            self.capture(output=link / "would-create")
        self.assertFalse((external / "would-create").exists())
        self.assertEqual((external / "keep").read_bytes(), b"untouched external bytes")
        with self.assertRaisesRegex(ValueError, "Linked observation path/ancestor"):
            current.inventory(link)

    def test_owner_dispatch_is_exact_and_has_no_raw_subprocess_fallback(self):
        for platform, module_name, method_name in (("win32", "ci_owned_process", "run_owned"),
                ("linux", "ci_owned_process", "run_owned"), ("darwin", "ci_mac_process", "run_mac_owned")):
            with self.subTest(platform=platform):
                owner = types.SimpleNamespace(run_owned=object(), run_mac_owned=object())
                spec = types.SimpleNamespace(loader=types.SimpleNamespace(exec_module=Mock()))
                with patch.object(current.sys, "platform", platform):
                    with patch.object(current.importlib.util, "spec_from_file_location", return_value=spec) as select:
                        with patch.object(current.importlib.util, "module_from_spec", return_value=owner):
                            self.assertIs(current.load_owner(), getattr(owner, method_name))
                selected_path = select.call_args.args[1]
                self.assertEqual(selected_path, ROOT / "scripts" / (module_name + ".py"))
                spec.loader.exec_module.assert_called_once_with(owner)

    def test_cli_forwards_only_explicit_paths_environment_and_deadlines(self):
        args = ["ci_current_observations.py", "--app", "installed-app", "--rgb-executable", "native-rgb",
                "--output", "fresh-output", "--root", "explicit-root", "--rgb-timeout", "600",
                "--gaussian-timeout", "90"]
        with patch.object(sys, "argv", args), patch.dict(current.os.environ, {"CONTROL": "value"}, clear=True):
            with patch.object(current, "capture_current") as capture:
                current.main()
                capture.assert_called_once_with(Path("installed-app"), Path("native-rgb"), Path("fresh-output"),
                                                {"CONTROL": "value"}, root=Path("explicit-root"),
                                                rgb_timeout=600, gaussian_timeout=90)
        with patch.object(sys, "argv", ["ci_current_observations.py", "--app", "missing-others"]):
            with patch.object(current, "capture_current") as capture, redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as refused:
                    current.main()
                self.assertEqual(refused.exception.code, 2)
                capture.assert_not_called()


if __name__ == "__main__":
    unittest.main()
