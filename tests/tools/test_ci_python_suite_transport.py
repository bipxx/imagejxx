"""Real complete-child CLI protocol using standalone synthetic pytest cases.

These tests do not import ImageJXX or qualify a product/platform. They require
the already-selected pytest test dependency and exercise actual collection,
JUnit and phase transport. All synthetic cases, logs and scratch are owned by
TemporaryDirectory; no compiler, dependency download or persistent cache is
used. Root verification owns execution of this module.
"""
import importlib.util
import importlib.machinery
from contextlib import ExitStack
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import sysconfig
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
import xml.etree.ElementTree as ET
import zipfile


ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts/ci_python_suite.py"
TIFF = "tests/python/test_tiff_runtime_policy.py::"
WINDOWS_IDS = (
    TIFF + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[none]",
    TIFF + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[missing_dll]",
    TIFF + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[changed_dll]",
    TIFF + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[changed_header]",
    TIFF + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[missing_link]",
    TIFF + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[wrong_headers]",
    TIFF + "test_cmake_fails_closed_for_runtime_header_and_linkage_changes[foreign_zlib]",
    TIFF + "test_recheck_refuses_changes_after_successful_configuration[bin/tiff.dll]",
    TIFF + "test_recheck_refuses_changes_after_successful_configuration[include/tiffio.h]",
    TIFF + "test_install_refuses_a_changed_dependency_before_copying_any_codec",
)
SHARED_IDS = tuple(TIFF + "test_reviewed_closure_rejects_restricted_or_changed_inputs[" + value + "]"
                   for value in ("none", "noncommercial", "version", "missing_dependency", "runtime_identity",
                                 "extra_dll", "notice", "cmake", "profile"))
PLAIN = "tests/python/test_complete_transport.py::test_plain"
OPAQUE = ("tests/python/test_complete_transport.py::TestOuter::TestInner::"
          "test_opaque[params.with::colons[brackets]]")
EXCLUSION_REASON = "The exact reviewed delivery profile is Windows x64"
UNSAFE_ENVIRONMENT = ("PYTHONOPTIMIZE", "PYTEST_ADDOPTS", "PYTEST_PLUGINS")


class CompleteChildTransport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if importlib.util.find_spec("pytest") is None:
            raise RuntimeError("Real complete-child transport tests require the existing pytest test dependency")

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx complete child transport ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        folder = self.root / "tests/python"
        folder.mkdir(parents=True)
        (self.root / "pytest.ini").write_text("[pytest]\naddopts = -q\n", encoding="utf-8")
        # Literal historical node names are synthetic transport witnesses here;
        # none of their actual product/policy implementations is copied/run.
        (folder / "test_tiff_runtime_policy.py").write_text(
            '"""SYNTHETIC protocol cases, not runtime-policy qualification."""\n'
            'import pytest\n'
            '@pytest.mark.parametrize("change", ["none", "noncommercial", "version", "missing_dependency", '
            '"runtime_identity", "extra_dll", "notice", "cmake", "profile"])\n'
            'def test_reviewed_closure_rejects_restricted_or_changed_inputs(change):\n'
            '    assert isinstance(change, str)\n'
            '@pytest.mark.parametrize("change", ["none", "missing_dll", "changed_dll", "changed_header", '
            '"missing_link", "wrong_headers", "foreign_zlib"])\n'
            'def test_cmake_fails_closed_for_runtime_header_and_linkage_changes(change):\n'
            '    assert isinstance(change, str)\n'
            '@pytest.mark.parametrize("relative", ["bin/tiff.dll", "include/tiffio.h"])\n'
            'def test_recheck_refuses_changes_after_successful_configuration(relative):\n'
            '    assert relative.startswith(("bin/", "include/"))\n'
            'def test_install_refuses_a_changed_dependency_before_copying_any_codec():\n'
            '    assert 2 + 2 == 4\n', encoding="utf-8")
        (folder / "test_complete_transport.py").write_text(
            '"""SYNTHETIC actual pytest collection and opaque ID witness."""\n'
            'from pathlib import Path\n'
            'import pytest\n'
            'Path(__file__).with_name("imported.marker").write_text("synthetic import observed", encoding="utf-8")\n'
            'def test_plain():\n'
            '    assert 3 * 7 == 21\n'
            'class TestOuter:\n'
            '    class TestInner:\n'
            '        @pytest.mark.parametrize("value", ["payload"], ids=["params.with::colons[brackets]"])\n'
            '        def test_opaque(self, value):\n'
            '            assert value == "payload"\n', encoding="utf-8")
        self.counter = 0

    def launch(self, *, environment=None, optimize=False):
        self.counter += 1
        output = self.root / ("output-" + str(self.counter))
        output.mkdir()
        child_environment = os.environ.copy()
        for key in (*UNSAFE_ENVIRONMENT, "PYTHONHOME", "PYTHONPATH", "PYTHONUSERBASE"):
            child_environment.pop(key, None)
        child_environment.update(PYTHONDONTWRITEBYTECODE="1", PYTEST_DISABLE_PLUGIN_AUTOLOAD="1")
        child_environment.update(environment or {})
        argv = [sys.executable, "-I", "-X", "utf8", "-B"]
        if optimize:
            argv.append("-O")
        argv += [str(RUNNER), "--complete-pytest-child", str(output), "--repository", str(self.root)]
        result = subprocess.run(argv, cwd=self.root, env=child_environment, capture_output=True,
                                text=True, encoding="utf-8", timeout=45, check=False)
        self.assertFalse((self.root / ".pytest_cache").exists())
        self.assertFalse((self.root / "tests/python/__pycache__").exists())
        return output, result

    def assert_refused_before_collection(self, output, result):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((self.root / "tests/python/imported.marker").exists())
        for name in ("complete-collection.json", "pytest.xml", "test-phases.jsonl", "active-test.json"):
            self.assertFalse((output / name).exists(), name)

    def test_real_cli_records_exact_collection_terminal_identity_and_all_phases(self):
        output, result = self.launch()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        collected = [PLAIN, OPAQUE, *SHARED_IDS, *WINDOWS_IDS]
        selected = [name for name in collected if sys.platform == "win32" or name not in WINDOWS_IDS]
        excluded = [] if sys.platform == "win32" else [
            {"id": name, "reason": EXCLUSION_REASON} for name in WINDOWS_IDS]
        collection = json.loads((output / "complete-collection.json").read_bytes())
        self.assertEqual(collection, {"schema": "imagejxx.complete-collection.001", "platform": sys.platform,
                                      "collected": collected, "selected": selected, "excluded": excluded})
        self.assertEqual((self.root / "tests/python/imported.marker").read_text(), "synthetic import observed")
        expected = {
            PLAIN: ("tests.python.test_complete_transport", "test_plain"),
            OPAQUE: ("tests.python.test_complete_transport.TestOuter.TestInner",
                     "test_opaque[params.with::colons[brackets]]"),
            **{name: ("tests.python.test_tiff_runtime_policy", name[len(TIFF):])
               for name in (*SHARED_IDS, *WINDOWS_IDS)}}
        rows = ET.parse(output / "pytest.xml").getroot().findall(".//testcase")
        self.assertEqual([(row.get("classname"), row.get("name")) for row in rows],
                         [expected[name] for name in selected])
        self.assertTrue(all(row.find(tag) is None for row in rows for tag in ("failure", "error", "skipped")))
        phases = [json.loads(line) for line in (output / "test-phases.jsonl").read_text().splitlines()]
        self.assertEqual([(row["id"], row["phase"]) for row in phases],
                         [(name, phase) for name in selected for phase in ("setup", "call", "teardown")])
        for row in phases:
            self.assertEqual(set(row), {"id", "phase", "outcome", "duration"})
            self.assertEqual(row["outcome"], "passed")
            self.assertIn(type(row["duration"]), (int, float))
            self.assertTrue(math.isfinite(row["duration"]) and row["duration"] >= 0)
        active = json.loads((output / "active-test.json").read_bytes())
        self.assertEqual(set(active), {"state", "id", "ended"})
        self.assertEqual((active["state"], active["id"]), ("finished", selected[-1]))
        self.assertTrue(math.isfinite(active["ended"]) and active["ended"] > 0)

    def test_inherited_pytest_addopts_cannot_reduce_collection_or_disable_assertions(self):
        for value in ("--ignore=tests/python/test_complete_transport.py", "--assert=plain"):
            with self.subTest(value=value):
                output, result = self.launch(environment={"PYTEST_ADDOPTS": value})
                self.assert_refused_before_collection(output, result)
                self.assertIn("PYTEST_ADDOPTS", result.stdout + result.stderr)

    def test_inherited_explicit_pytest_plugin_is_refused_even_with_autoload_disabled(self):
        # This bundled pytest plugin is importable; a missing-plugin error must
        # not accidentally stand in for the required environment refusal.
        output, result = self.launch(environment={"PYTEST_PLUGINS": "_pytest.pytester"})
        self.assert_refused_before_collection(output, result)
        self.assertIn("PYTEST_PLUGINS", result.stdout + result.stderr)

    def test_inherited_pythonoptimize_is_refused_even_when_isolation_ignores_it(self):
        output, result = self.launch(environment={"PYTHONOPTIMIZE": "1"})
        self.assert_refused_before_collection(output, result)
        self.assertIn("PYTHONOPTIMIZE", result.stdout + result.stderr)

    def test_explicit_optimized_interpreter_is_refused_before_pytest_import(self):
        output, result = self.launch(optimize=True)
        self.assert_refused_before_collection(output, result)
        self.assertIn("enabled assertions", result.stdout + result.stderr)


class CompleteParentTransport(unittest.TestCase):
    """Real smoke/pytest children; product imports and C++ capture are stubs.

    This checks the maintained parent-to-child protocol and failure ordering,
    never an installed image processor, codec, render context or C++ binary.
    """
    def setUp(self):
        if importlib.util.find_spec("pytest") is None:
            raise RuntimeError("Parent transport tests require the existing pytest dependency")
        CompleteChildTransport.setUp(self)
        spec = importlib.util.spec_from_file_location("ci2_parent_transport", RUNNER)
        self.runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.runner)
        self.output = self.root / "complete-output"
        self.product = self.root / "synthetic-installed/imagejxx"
        self.product.mkdir(parents=True)
        modules = {}
        for name in ("imagejxx", "imagejxx._native", "imagejxx._scientific_io", "imagejxx._desktop"):
            file = self.product / ("__init__.py" if name == "imagejxx" else name.rsplit(".", 1)[1] + ".stub")
            file.write_bytes(b"SYNTHETIC product identity only; never imported\n")
            modules[name] = SimpleNamespace(__file__=str(file))
        (self.product / "ui").mkdir()
        (self.product / "ui/app.py").write_bytes(b"# SYNTHETIC adapter identity\n")
        executable = Path("bin") / ("imagejxx.exe" if sys.platform == "win32" else "imagejxx")
        application = self.product.parent / executable
        application.parent.mkdir()
        application.write_bytes(b"SYNTHETIC application identity; capture is mocked\n")
        application.chmod(0o755)
        distribution = SimpleNamespace(files=[executable], locate_file=lambda name: (
            self.product if str(name) == "imagejxx" else self.product.parent / name))
        provider = self.root / "build/wheel" / ("_imagejxx_test_saved_roi_source" + sysconfig.get_config_var("EXT_SUFFIX"))
        provider.parent.mkdir(parents=True)
        provider.write_bytes(b"SYNTHETIC provider identity; loader is mocked\n")

        class ProviderLoader:
            def create_module(self, spec):
                return None
            def exec_module(self, module):
                module.abi_sizes = lambda: {"image": 64, "shared_const_image": 16}

        original_spec = importlib.util.spec_from_file_location
        original_import = self.runner.importlib.import_module
        def provider_spec(name, path, *args, **kwargs):
            if name == "_imagejxx_test_saved_roi_source":
                return importlib.machinery.ModuleSpec(name, ProviderLoader())
            return original_spec(name, path, *args, **kwargs)
        stack = ExitStack()
        self.addCleanup(stack.close)
        stack.enter_context(patch.object(self.runner.importlib.metadata, "distribution", return_value=distribution))
        stack.enter_context(patch.object(self.runner.importlib, "import_module", side_effect=lambda name, *a, **kw:
            modules[name] if name in modules else original_import(name, *a, **kw)))
        stack.enter_context(patch.object(self.runner.importlib.util, "spec_from_file_location", side_effect=provider_spec))
        stack.enter_context(patch.object(self.runner, "audit_static_codecs", return_value={"scope": "synthetic transport"}))
        # This fixture exercises child transport, not historical reference availability.
        stack.enter_context(patch.object(self.runner, "reference_readiness", return_value={
            "state": "ready", "scope": "synthetic parent transport only", "acceptance_claim": False}))
        def current_capture(output, application, root, environment):
            self.assertEqual(json.loads((self.output / "installed-smoke/receipt.json").read_bytes())["state"], "passed")
            output.mkdir()
            (output / "receipt.json").write_bytes(b'{"state":"captured","commands":[],"scope":"synthetic current-producer transport only"}\n')
            return {name: str(output / (name + ".json")) for name in (
                "IMAGEJXX_RGB_COMBINED_INPUT_REPORT", "IMAGEJXX_H1_CPP_REPORT", "IMAGEJXX_R16_CPP_REPORT")}
        stack.enter_context(patch.object(self.runner, "capture_current_observations", side_effect=current_capture))
        self.capture_calls = []
        def capture(output, application, root, environment):
            smoke = json.loads((self.output / "installed-smoke/receipt.json").read_bytes())
            self.assertEqual(smoke["state"], "passed")
            for name in UNSAFE_ENVIRONMENT:
                self.assertNotIn(name, environment)
            for name in ("IMAGEJXX_RENDER_TESTS", "IMAGEJXX_REQUIRE_RENDER_TESTS"):
                self.assertEqual(environment[name], "1")
            for name in ("IMAGEJXX_RGB_COMBINED_INPUT_REPORT", "IMAGEJXX_H1_CPP_REPORT", "IMAGEJXX_R16_CPP_REPORT"):
                self.assertTrue(Path(environment[name]).is_relative_to(self.output / "current-observations"))
            self.capture_calls.append((output, application))
            output.mkdir()
            (output / "receipt.json").write_text('{"scope":"synthetic C++ capture stub"}\n', encoding="utf-8")
            return {}
        stack.enter_context(patch.object(self.runner, "capture", side_effect=capture))
        # Synthetic headless evidence tests only the new join; actual product
        # singleton execution is separately qualified through --headless-suite.
        folder = self.root / "tests/python"
        for module, functions in (("test_history_settings.py", self.runner.HEADLESS_TEST_IDS[:4]),
                                  ("test_shortcuts.py", self.runner.HEADLESS_TEST_IDS[4:])):
            (folder / module).write_text('"""SYNTHETIC routed IDs, no product semantics."""\n' + "".join(
                "def " + name.split("::")[-1] + "():\n    assert True\n" for name in functions), encoding="utf-8")
        def isolated(output, root, environment, *, deadline, on_start):
            self.assertGreater(deadline, self.runner.time.monotonic())
            output.mkdir()
            (output / "receipt.json").write_bytes(b'{"scope":"SYNTHETIC headless join fixtures only"}')
            reports = []
            for name in self.runner.HEADLESS_TEST_IDS:
                classname, testname = self.runner.junit_identity(name)
                reports.append({"id": name, "collection": {
                    "schema": "imagejxx.headless-collection.001", "platform": sys.platform,
                    "collected": [name], "selected": [name], "excluded": []},
                    "junit_rows": [ET.Element("testcase", classname=classname, name=testname)],
                    "phases": [{"id": name, "phase": phase, "outcome": "passed", "duration": 0.001}
                               for phase in ("setup", "call", "teardown")],
                    "process": {"schema": "imagejxx.owned-command.001", "state": "exited", "exit_code": 0,
                                "target_completed": True, "cleanup_complete": True}})
            return reports
        stack.enter_context(patch.object(self.runner, "run_headless_tests", side_effect=isolated))
        self.write_smoke()

    def test_current_producer_failure_keeps_resolved_raw_diagnostics(self):
        def failed(output, *args):
            output.mkdir()
            (output / "receipt.json").write_bytes(b'{"state":"failed","commands":[{"cleanup_complete":true}]}')
            (output / "failure.json").write_bytes(b'SYNTHETIC resolved current producer failure')
            raise ValueError("synthetic current producer failure")
        with patch.object(self.runner, "capture_current_observations", side_effect=failed):
            with self.assertRaisesRegex(ValueError, "synthetic current producer failure"):
                self.runner.run(self.output, self.root)
        self.assertFalse(self.capture_calls)
        with zipfile.ZipFile(self.output / "current-observations.zip") as bundle:
            self.assertEqual(bundle.read("failure.json"), b'SYNTHETIC resolved current producer failure')

    def test_current_producer_without_cleanup_proof_is_not_archived(self):
        def unresolved(output, *args):
            output.mkdir()
            (output / "receipt.json").write_bytes(b'{"state":"failed","commands":[{"cleanup_complete":false}]}')
            raise ValueError("synthetic unresolved current producer")
        with patch.object(self.runner, "capture_current_observations", side_effect=unresolved):
            with self.assertRaisesRegex(ValueError, "synthetic unresolved current producer"):
                self.runner.run(self.output, self.root)
        receipt = json.loads((self.output / "receipt.json").read_bytes())
        self.assertEqual(receipt["current_observation_archive"]["state"], "unavailable")
        self.assertFalse((self.output / "current-observations.zip").exists())
        self.assertFalse(self.capture_calls)

    def test_current_archive_failure_is_a_nonzero_parent_outcome(self):
        original = self.runner.archive
        def fail_current(path):
            if path.name == "current-observations":
                raise OSError("synthetic current archive failure")
            return original(path)
        with patch.object(self.runner, "archive", side_effect=fail_current):
            with self.assertRaisesRegex(OSError, "synthetic current archive failure"):
                self.runner.run(self.output, self.root)
        receipt = json.loads((self.output / "receipt.json").read_bytes())
        self.assertEqual(receipt["state"], "failed")
        self.assertIn("synthetic current archive failure", receipt["current_archive_error"]["message"])
        self.assertTrue((self.output / "cpp-observations.zip").is_file())

    def test_unresolved_complete_reader_withholds_both_capture_archives(self):
        original = self.runner.complete_command
        def unresolved(argv, *, output, **kwargs):
            if output.name == "installed-smoke":
                return original(argv, output=output, **kwargs)
            (output / "process.json").write_bytes(b'{"schema":"imagejxx.owned-command.001","state":"cleanup_failed","cleanup_complete":false}')
            raise ValueError("synthetic unresolved complete reader")
        with patch.object(self.runner, "complete_command", side_effect=unresolved):
            with self.assertRaisesRegex(ValueError, "synthetic unresolved complete reader"):
                self.runner.run(self.output, self.root)
        receipt = json.loads((self.output / "receipt.json").read_bytes())
        self.assertEqual(receipt["state"], "failed")
        for key in ("current_observation_archive", "cpp_observation_archive"):
            self.assertEqual(receipt[key]["state"], "unavailable")
        self.assertFalse((self.output / "current-observations.zip").exists())
        self.assertFalse((self.output / "cpp-observations.zip").exists())

    def test_failed_capture_inventory_refusal_never_invokes_archive(self):
        for reason in ("linked path refused", "capture inventory exceeds bound"):
            with self.subTest(reason=reason):
                # Each attempt owns a distinct fresh parent output.
                self.output = self.root / ("refused-output-" + str(len(reason)))
                def failed(output, *args):
                    output.mkdir()
                    (output / "receipt.json").write_bytes(b'{"state":"failed","commands":[{"cleanup_complete":true}]}')
                    raise ValueError("synthetic failed producer")
                with patch.object(self.runner, "capture_current_observations", side_effect=failed), \
                     patch.object(self.runner, "current_capture_inventory", side_effect=ValueError(reason)), \
                     patch.object(self.runner, "archive", side_effect=AssertionError("refused output must not be traversed")) as archiver:
                    with self.assertRaisesRegex(ValueError, "synthetic failed producer"):
                        self.runner.run(self.output, self.root)
                archiver.assert_not_called()
                receipt = json.loads((self.output / "receipt.json").read_bytes())
                self.assertEqual(receipt["current_observation_archive"]["state"], "unavailable")
                self.assertEqual(receipt["current_archive_error"]["message"], reason)
                self.assertFalse((self.output / "current-observations.zip").exists())

    def test_unresolved_headless_reader_withholds_shared_capture_archives(self):
        def unresolved(output, root, environment, *, deadline, on_start):
            case = output / "case-01"
            case.mkdir(parents=True)
            on_start(case / "process.json")
            (case / "process.json").write_bytes(b'{"schema":"imagejxx.owned-command.001","state":"cleanup_failed","cleanup_complete":false}')
            raise ValueError("synthetic unresolved isolated reader")
        with patch.object(self.runner, "run_headless_tests", side_effect=unresolved):
            with self.assertRaisesRegex(ValueError, "synthetic unresolved isolated reader"):
                self.runner.run(self.output, self.root)
        receipt = json.loads((self.output / "receipt.json").read_bytes())
        self.assertFalse(receipt["complete_reader_started"])
        self.assertEqual(len(receipt["reader_processes"]), 1)
        self.assertFalse((self.output / "process.json").exists())
        for key in ("current_observation_archive", "cpp_observation_archive"):
            self.assertEqual(receipt[key]["state"], "unavailable")
        self.assertEqual(receipt["python_diagnostics"]["headless-01"]["state"], "unavailable")
        self.assertFalse((self.output / "current-observations.zip").exists())
        self.assertFalse((self.output / "cpp-observations.zip").exists())

    def test_resolved_failing_headless_reader_preserves_actual_diagnostic_payload(self):
        def failed(output, root, environment, *, deadline, on_start):
            case = output / "case-01"
            (case / "pytest-temp").mkdir(parents=True)
            on_start(case / "process.json")
            (case / "process.json").write_bytes(b'{"schema":"imagejxx.owned-command.001","state":"exited","exit_code":1,"target_completed":true,"cleanup_complete":true}')
            (case / "pytest-temp/failure.json").write_bytes(b'SYNTHETIC isolated reader failure')
            raise subprocess.CalledProcessError(1, ["SYNTHETIC isolated child"])
        with patch.object(self.runner, "run_headless_tests", side_effect=failed):
            with self.assertRaises(subprocess.CalledProcessError):
                self.runner.run(self.output, self.root)
        receipt = json.loads((self.output / "receipt.json").read_bytes())
        diagnostic = receipt["python_diagnostics"]["headless-01"]
        self.assertEqual(diagnostic["state"], "complete")
        with zipfile.ZipFile(diagnostic["archive"]["path"]) as bundle:
            self.assertEqual(bundle.read(diagnostic["members"][0]["member"]), b'SYNTHETIC isolated reader failure')
        self.assertTrue((self.output / "current-observations.zip").is_file())
        self.assertTrue((self.output / "cpp-observations.zip").is_file())
        self.assertFalse(receipt["complete_reader_started"])

    def test_empty_optional_shortcut_transport_is_refused_before_smoke(self):
        with patch.dict(os.environ, {"IMAGEJXX_SHORTCUTS_PYTHON_OBSERVATIONS": ""}):
            with self.assertRaisesRegex(ValueError, "separate routing contract"):
                self.runner.run(self.output, self.root)
        self.assertFalse((self.output / "installed-smoke").exists())
        self.assertFalse(self.capture_calls)

    def write_smoke(self, *, skip_render=False):
        (self.root / "tests/python/test_scientific_tiff_io.py").write_text(
            'import pytest\n@pytest.mark.parametrize("case", ["defaults.tif"], ids=["defaults.tif"])\n'
            'def test_independent_carriers_and_admission(case):\n    assert case == "defaults.tif"\n', encoding="utf-8")
        body = ('import pytest\n'
                '    (tmp_path / "failure.json").write_text("SYNTHETIC smoke diagnostic")\n'
                '    pytest.skip("synthetic unavailable required render")') if skip_render else 'assert 11 * 11 == 121'
        (self.root / "tests/python/test_settings.py").write_text(
            'def test_settings_in_hidden_opengl_and_recreated_context(tmp_path):\n    ' + body + '\n', encoding="utf-8")

    def test_parent_cleans_overrides_and_joins_real_smoke_and_complete_children(self):
        with patch.dict(os.environ, {"PYTEST_ADDOPTS": "--ignore=tests/python/test_complete_transport.py",
                                    "PYTEST_PLUGINS": "_pytest.pytester", "PYTHONOPTIMIZE": "2"}):
            self.runner.run(self.output, self.root)
        receipt = json.loads((self.output / "receipt.json").read_bytes())
        self.assertEqual(receipt["state"], "passed")
        self.assertEqual(len(self.capture_calls), 1)
        self.assertEqual(receipt["counts"], {"cases": 28 if sys.platform == "win32" else 18,
                                            "failure": 0, "error": 0, "skipped": 0})
        self.assertTrue((self.output / "cpp-observations.zip").is_file())
        self.assertTrue((self.output / "test-phases.jsonl").is_file())
        self.assertFalse((self.root / ".pytest_cache").exists())

    def test_required_smoke_skip_stops_before_capture_or_complete_collection(self):
        self.write_smoke(skip_render=True)
        with self.assertRaisesRegex(ValueError, "both exact passing cases"):
            self.runner.run(self.output, self.root)
        receipt = json.loads((self.output / "receipt.json").read_bytes())
        self.assertEqual(receipt["state"], "failed")
        self.assertEqual(self.capture_calls, [])
        self.assertFalse((self.output / "complete-collection.json").exists())
        smoke = json.loads((self.output / "installed-smoke/receipt.json").read_bytes())
        self.assertEqual(smoke["state"], "failed")
        diagnostic = receipt["python_diagnostics"]["installed-smoke"]
        self.assert_diagnostics_retained(diagnostic)
        self.assertEqual(len(diagnostic["members"]), 1)
        with zipfile.ZipFile(diagnostic["archive"]["path"]) as bundle:
            self.assertEqual(bundle.read(diagnostic["members"][0]["member"]), b"SYNTHETIC smoke diagnostic")

    def test_complete_child_failure_retains_diagnostics_after_owned_cleanup(self):
        (self.root / "tests/python/test_failed_producer.py").write_text(
            'def test_failure(tmp_path):\n'
            '    (tmp_path / "failure.json").write_text("SYNTHETIC complete diagnostic")\n'
            '    assert False, "intentional synthetic producer failure"\n', encoding="utf-8")
        with self.assertRaises(subprocess.CalledProcessError):
            self.runner.run(self.output, self.root)
        receipt = json.loads((self.output / "receipt.json").read_bytes())
        self.assertEqual(receipt["state"], "failed")
        self.assertEqual(receipt["exit_code"], 1)
        self.assertEqual(len(self.capture_calls), 1)
        process = json.loads((self.output / "process.json").read_bytes())
        self.assertTrue(process["cleanup_complete"])
        self.assertEqual(process["exit_code"], 1)
        diagnostic = receipt["python_diagnostics"]["complete"]
        self.assert_diagnostics_retained(diagnostic)
        self.assertEqual(len(diagnostic["members"]), 1)
        with zipfile.ZipFile(diagnostic["archive"]["path"]) as bundle:
            self.assertEqual(bundle.read(diagnostic["members"][0]["member"]), b"SYNTHETIC complete diagnostic")

    def test_unresolved_process_receipt_does_not_read_or_archive_live_scratch(self):
        self.output.mkdir()
        scratch = self.output / "pytest-temp"
        scratch.mkdir()
        source = scratch / "failure.json"
        source.write_bytes(b"SYNTHETIC potentially live diagnostic")
        (self.output / "process.json").write_text(json.dumps({
            "schema": "imagejxx.owned-command.001", "state": "cleanup_failed", "cleanup_complete": False}), encoding="utf-8")
        result = self.runner.failed_python_diagnostics(self.output)
        self.assertEqual(result["complete"], {"state": "unavailable", "reason": "process_cleanup_unresolved"})
        self.assertFalse((self.output / "complete-python-diagnostics.zip").exists())
        self.assertEqual(source.read_bytes(), b"SYNTHETIC potentially live diagnostic")

    def assert_diagnostics_retained(self, diagnostic):
        # POSIX pytest creates convenience '*current' symlinks beside the real
        # fixture directories. Retention reports those omissions explicitly;
        # it must still retain every actual diagnostic and never follow aliases.
        self.assertIn(diagnostic["state"], ("complete", "incomplete"))
        self.assertEqual(diagnostic["state"], "incomplete" if diagnostic["omitted"] else "complete")
        self.assertEqual(diagnostic["discovery"]["complete"], not diagnostic["omitted"], diagnostic)
        for omission in diagnostic["omitted"]:
            self.assertEqual(omission["reason"], "linked_path")
            self.assertTrue(omission["path"].endswith("current"), omission)


if __name__ == "__main__":
    unittest.main()
