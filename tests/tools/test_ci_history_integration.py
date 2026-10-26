"""Synthetic admission/orchestration for fixed history and appearance routes.

These controls execute no installed application, pytest child, pip or compiler.
The independent identity list, file hashes, source membership and JUnit parsing
are real; owner/smoke/producer fixtures are explicitly synthetic transport only.
They confer no scientific or actual UI acceptance.
"""
from copy import deepcopy
from contextlib import redirect_stderr
import base64
import csv
import hashlib
import importlib.metadata
import importlib.util
import io
import json
import os
from pathlib import Path
import py_compile
import subprocess
import sys
import sysconfig
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "ci_history_integration_under_test", ROOT / "scripts/ci_python_suite.py")
suite = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(suite)
BUILD_SPEC = importlib.util.spec_from_file_location(
    "ci_history_builder_for_integration", ROOT / "scripts/ci_history_build.py")
builder = importlib.util.module_from_spec(BUILD_SPEC)
BUILD_SPEC.loader.exec_module(builder)

HISTORY_IDS = tuple(
    "tests/python/test_history_settings_ui.py::test_installed_history_settings[" + case + "-" + context + "]"
    for case in ("HU01", "HU02", "HU03", "HU04")
    for context in ("imgui_cpp", "imgui_python"))
APPEARANCE_IDS = tuple(
    "tests/python/test_settings_appearance_ui.py::test_installed_settings_appearance[" + case + "-" + context + "]"
    for case in ("appearance-persist", "appearance-reopen")
    for context in ("imgui_cpp", "imgui_python"))
APPEARANCE_ENV = "IMAGEJXX_SETTINGS_APPEARANCE_UI_CASE_ROOT"
DRAWING_IDS = tuple(
    "tests/python/test_settings_drawing_ui.py::test_installed_settings_drawing[" + case + "-" + context + "]"
    for case in ("drawing-persist", "drawing-reopen") for context in ("imgui_cpp", "imgui_python"))
DRAWING_ENV = "IMAGEJXX_SETTINGS_DRAWING_UI_CASE_ROOT"
PACKET_IDS = {"history": HISTORY_IDS, "appearance": APPEARANCE_IDS, "drawing": DRAWING_IDS}
HISTORY_ROLES = (
    "tests/python/actual_observation_transport.py", "src/desktop/actual_observation_transport.hpp",
    "src/desktop/window.cpp", "src/desktop/headless_session_internal.hpp",
    "src/desktop/settings_panel.cpp", "src/desktop/settings_panel.hpp",
    "src/native/settings.cpp", "src/native/preferences.cpp", "src/desktop/python.cpp",
    "src/imagejxx/ui/app.py", "docs/parity/commands.json",
    "scripts/capture_shortcuts_history_ui.py", "scripts/ci_owned_process.py",
    "src/imagejxx/__init__.py", "src/imagejxx/ui/persistence.py",
    "src/desktop/history_settings_ui_probe.cpp", "src/desktop/history_settings_ui_probe.hpp",
    "scripts/ci_mac_process.py", "src/native/document.cpp", "include/imagejxx/document.hpp",
    "src/native/application.cpp", "include/imagejxx/application.hpp", "src/native/session.cpp",
    "include/imagejxx/session.hpp", "tests/python/test_history_settings_ui.py",
    "docs/parity/reviews/G010-history-settings-ownership-001.json",
)
APPEARANCE_EXTRA_ROLES = (
    "src/desktop/settings_appearance_ui_probe.cpp", "src/desktop/settings_appearance_ui_probe.hpp",
    "src/desktop/window.hpp", "src/desktop/orthogonal.cpp", "src/desktop/orthogonal.hpp",
    "src/desktop/planar_annotation_panel.cpp", "src/desktop/planar_annotation_panel.hpp",
    "src/desktop/path_authoring_controls.cpp", "src/desktop/path_authoring_edge_ui_probe.cpp",
    "src/desktop/path_authoring_ui_probe.cpp", "src/desktop/path_segment_ui_probe.cpp",
    "src/desktop/rendering.cpp", "src/desktop/settings_io.cpp", "tests/python/test_settings_appearance_ui.py",
    "tests/python/settings_appearance_reader.py", "tests/python/settings_appearance_expectations.py",
)
DRAWING_EXTRA_ROLES = (
    "src/desktop/settings_drawing_ui_probe.cpp", "src/desktop/settings_drawing_ui_probe.hpp",
    "src/desktop/window.hpp", "src/desktop/selection_panel.cpp", "src/native/selection_edit.cpp",
    "src/native/roi_style.cpp", "src/native/transforms.cpp", "src/native/document.cpp",
    "src/native/operations.cpp", "src/native/operation_kernel.inc", "src/desktop/rendering.cpp",
    "src/desktop/settings_io.cpp", "scripts/ci_owned_process.py", "scripts/ci_mac_process.py",
    "tests/python/test_settings_drawing_ui.py", "tests/python/settings_drawing_reader.py",
    "tests/python/settings_drawing_expectations.py", "tests/native/rounded_path_reference.inc",
)
MANDATORY_SOURCES = (
    "CMakeLists.txt", "pyproject.toml", "README.md", "LICENSE", "THIRD_PARTY_NOTICES.md",
    ".gitattributes", "scripts/ci_history_build.py", "scripts/ci_python_suite.py",
    ".github/workflows/test.yml", ".github/workflows/source-platform.yml",
    "src/desktop/command_metadata.hpp",
)
TRACKED_SOURCES = MANDATORY_SOURCES + (
    "src/native/fixture.cpp", "include/imagejxx/fixture.hpp",
    "examples/cpp/header.hpp", "examples/cpp/entry.cpp",
    "third_party/fixture/header.hpp", "cmake/Fixture.cmake", "licenses/fixture.txt", "conftest.py",
)
MODULE_NAMES = ("imagejxx", "imagejxx._native", "imagejxx._desktop",
                "imagejxx.ui.app", "imagejxx.ui.persistence")


def pin(path):
    path = Path(path).resolve(strict=True)
    raw = path.read_bytes()
    return {"path": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def junit_identity(nodeid):
    module, function = nodeid.split("::", 1)
    return module[:-3].replace("/", "."), function


class HistoryIntegration(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx-history-integration-metadata-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve(strict=True)
        sysconfig.get_config_var("EXT_SUFFIX")  # Prime native config before the synthetic platform mock.
        if sys.platform == "darwin":
            # Only the synthetic metadata profile is Linux on this host; no
            # owner or native build is invoked or qualified by these controls.
            self.start_patch(patch.object(builder.sys, "platform", "linux"))
        self.output = self.root / "build/history-suite"
        self.build_dir = self.root / "build/history-build"
        self.build_path = self.build_dir / "receipt.json"
        self.freeze_path = self.build_dir / "source-freeze.json"
        self.process_path = self.build_dir / "process.json"
        self.package = self.root / "installed/site-packages/imagejxx"
        self.site = self.package.parent
        self.dist_info = self.site / "imagejxx-0.2.0.dist-info"
        self.distribution = importlib.metadata.PathDistribution(self.dist_info)
        app_name = "imagejxx.exe" if sys.platform == "win32" else "imagejxx"
        self.application = self.write("installed/bin/" + app_name, b"synthetic cpp bytes; never executed\n")
        self.engine = self.write("interpreter/python", b"synthetic engine identity; never executed\n")
        self.tracked = list(TRACKED_SOURCES)
        self.source_names = sorted(set(HISTORY_ROLES + TRACKED_SOURCES))
        for logical in self.source_names:
            self.write(logical, ("synthetic frozen source: " + logical + "\n").encode())
        extension = sysconfig.get_config_var("EXT_SUFFIX")
        suffixes = {"imagejxx": "__init__.py", "imagejxx._native": "_native" + extension,
                    "imagejxx._desktop": "_desktop" + extension, "imagejxx.ui.app": "ui/app.py",
                    "imagejxx.ui.persistence": "ui/persistence.py"}
        self.module_objects = {}
        for name, relative in suffixes.items():
            path = self.write("installed/site-packages/imagejxx/" + relative,
                              ("synthetic installed " + name + "; never imported\n").encode())
            self.module_objects[name] = SimpleNamespace(__file__=str(path))
        numpy_path = self.write("dependencies/numpy/__init__.py", b"synthetic NumPy metadata\n")
        self.numpy = SimpleNamespace(__file__=str(numpy_path), __version__="2.2.6")
        self.modules = {name: pin(value.__file__) for name, value in self.module_objects.items()}
        self.helper_pin = pin(self.root / "scripts/ci_owned_process.py")
        self.argv = [str(Path(sys.executable).resolve(strict=True)), "-m", "pip", "install", ".[io,test]",
                     "-C", "cmake.build-type=Release", "-Ccmake.define.BUILD_TESTING=ON"]
        self.build_process = self.process(self.argv)
        cache = self.write("build/wheel/synthetic/CMakeCache.txt", (
            "# synthetic metadata only; never configured\n"
            "CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_BUILD_TYPE:STRING=Release\n"
            "CMAKE_CONFIGURATION_TYPES:STRING=\nBUILD_TESTING:BOOL=ON\n"
            "IMAGEJXX_BUILD_PYTHON:BOOL=ON\nIMAGEJXX_BUILD_IO:BOOL=ON\n"
            "IMAGEJXX_BUILD_CPP_UI:BOOL=ON\nIMAGEJXX_BUILD_PYTHON_UI:BOOL=ON\n"
            "CMAKE_HOME_DIRECTORY:INTERNAL=" + str(self.root) + "\n"
            "CMAKE_CACHEFILE_DIR:INTERNAL=" + str(self.root / "build/wheel/synthetic") + "\n").encode())
        final_paths = {"cpp": self.application,
                       "_native": Path(self.module_objects["imagejxx._native"].__file__),
                       "_desktop": Path(self.module_objects["imagejxx._desktop"].__file__)}
        self.build_document = {
            "schema_version": 1, "kind": "imagejxx.hosted-history-build.001",
            "state": "passed", "acceptance": False, "argv": self.argv,
            "repository": str(self.root), "cache": pin(cache),
            "launch_executable": pin(Path(sys.executable).resolve(strict=True)),
            "timeout_seconds": 17,
            "source_times": {"frozen": 1.0, "verified_after_command": 2.0, "verified_final": 3.0},
            "configuration": {
                "CMAKE_GENERATOR": "Ninja", "CMAKE_BUILD_TYPE": "Release", "BUILD_TESTING": "ON",
                "IMAGEJXX_BUILD_PYTHON": "ON", "IMAGEJXX_BUILD_IO": "ON",
                "IMAGEJXX_BUILD_CPP_UI": "ON", "IMAGEJXX_BUILD_PYTHON_UI": "ON",
                "CMAKE_HOME_DIRECTORY": str(self.root),
                "CMAKE_CACHEFILE_DIR": str(self.root / "build/wheel/synthetic"),
                "IMAGEJXX_CI_SOURCE_PROFILE": "",
            },
            "log": pin(self.write("build/history-build/build.log", b"synthetic terminal log; no build executed\n")),
            "owner_helper": self.helper_pin, "command_proof": deepcopy(self.build_process),
            "raw_cache_products": {
                role: pin(self.write("build/wheel/synthetic/" + path.name,
                                    ("synthetic raw linker output " + role + "; never executed\n").encode()))
                for role, path in final_paths.items()},
            "built_products": {role: pin(path) for role, path in final_paths.items()},
            "packaging": self.make_packaging(final_paths),
        }
        self.freeze_document = {"schema_version": 1, "repository": str(self.root),
                                "sources": [{"logical_path": name, **pin(self.root / name)}
                                            for name in self.source_names]}
        self.write_build()
        self.start_patch(patch.object(suite, "history_builder", return_value=builder))
        self.start_patch(patch.object(builder, "_tracked_paths", side_effect=lambda root: list(self.tracked)))
        self.start_patch(patch.object(builder, "_history_paths", return_value=set(HISTORY_ROLES)))
        self.start_patch(patch.object(importlib.metadata, "distribution", side_effect=self.installed_distribution))
        self.capture_helper = suite.history_helper()
        self.start_patch(patch.object(suite, "history_helper", return_value=self.capture_helper))
        self.start_patch(patch.object(self.capture_helper, "engine", return_value=self.engine))
        self.start_patch(patch.object(subprocess, "Popen", side_effect=AssertionError(
            "Synthetic history admission controls cannot execute a process")))
        self.start_patch(patch.object(sys, "dont_write_bytecode", True))
        self.start_patch(patch.dict(os.environ, {}, clear=True))
        self.events = []
        self.commands = []
        self.fail_stage = None
        self.proof_override = {}

    def start_patch(self, value):
        result = value.start()
        self.addCleanup(value.stop)
        return result

    def write(self, relative, raw):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
        return path

    def save(self, path, value):
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")
        return pin(path)

    def read(self, path):
        return json.loads(Path(path).read_bytes())

    def process(self, argv=None):
        return {"schema": "imagejxx.owned-command.001", "state": "exited", "exit_code": 0,
                "target_completed": True, "cleanup_complete": True, "timed_out": False,
                "argv": list(argv or [str(self.engine), "synthetic-transport-only"]),
                "cwd": str(self.root), "helper": deepcopy(self.helper_pin)}

    def installed_distribution(self, name):
        self.assertEqual(name, "imagejxx")
        return self.distribution

    @staticmethod
    def record_row(logical, path):
        raw = Path(path).read_bytes()
        encoded = base64.urlsafe_b64encode(hashlib.sha256(raw).digest()).rstrip(b"=").decode("ascii")
        return [logical, "sha256=" + encoded, str(len(raw))]

    def write_record(self):
        with self.record_path.open("w", encoding="utf-8", newline="") as stream:
            csv.writer(stream, lineterminator="\n").writerows(self.record_rows)
        return pin(self.record_path)

    def make_packaging(self, final_paths):
        """Original tiny metadata files; no distribution/provider bypass needed."""
        self.dist_info.mkdir(parents=True)
        metadata = self.dist_info / "METADATA"
        metadata.write_bytes(b"Metadata-Version: 2.1\nName: imagejxx\nVersion: 0.2.0\n\n")
        direct_url = self.dist_info / "direct_url.json"
        direct_url.write_text(json.dumps({"url": self.root.as_uri(), "dir_info": {}}) + "\n",
                              encoding="utf-8")
        self.record_path = self.dist_info / "RECORD"
        self.record_rows = []
        products = {}
        for role, path in final_paths.items():
            logical = Path(os.path.relpath(path, self.site)).as_posix()
            row = self.record_row(logical, path)
            self.record_rows.append(row)
            products[role] = {"record_path": logical, "record_hash": row[1], "record_size": int(row[2])}
        for path in (metadata, direct_url):
            self.record_rows.append(self.record_row(path.relative_to(self.site).as_posix(), path))
        self.record_rows.append([self.dist_info.name + "/RECORD", "", ""])
        return {"phase": "pip-installed-final-outputs", "products": products,
                "record": self.write_record(), "metadata": pin(metadata), "direct_url": pin(direct_url),
                "distribution_name": "imagejxx", "distribution_version": "0.2.0",
                "source_url": self.root.as_uri(),
                "raw_to_final_relation": "authenticated build-and-install pipeline association; not a bytewise strip-derivation proof",
                "strip_policy": "backend install policy unchanged; transient wheel/staging bytes are not retained or reconstructed"}

    def write_build(self):
        self.build_document["process"] = self.save(self.process_path, self.build_process)
        self.build_document["command_proof"] = deepcopy(self.build_process)
        self.build_document["source_freeze"] = self.save(self.freeze_path, self.freeze_document)
        self.save(self.build_path, self.build_document)

    def bindings(self, *, kind="history"):
        application = pin(self.application)
        prefix = {"history": "p06h", "appearance": "c2", "drawing": "c3"}[kind]
        return {"schema_version": 1, "kind": prefix + ".installed-ui-capture-bindings.001",
                "kind_selection": kind, "build_receipt": pin(self.build_path),
                "source_freeze": self.build_document["source_freeze"],
                "built_products": self.build_document["built_products"],
                "installed_modules": deepcopy(self.modules), "python_engine": pin(self.engine),
                "products": {
                    "imgui_cpp": {key: application for key in ("executable", "producer_artifact", "native_artifact")},
                    "imgui_python": {"executable": pin(self.engine),
                                     "producer_artifact": self.modules["imagejxx._desktop"],
                                     "native_artifact": self.modules["imagejxx._desktop"]}},
                "numpy": {"version": "2.2.6", "site_packages": str(self.root / "dependencies"),
                          "module": pin(self.numpy.__file__)}}

    def write_junit(self, path, ids=HISTORY_IDS, outcome=None):
        tree = ET.Element("testsuites")
        group = ET.SubElement(tree, "testsuite", tests=str(len(ids)))
        for nodeid in ids:
            classname, name = junit_identity(nodeid)
            case = ET.SubElement(group, "testcase", classname=classname, name=name, time="0.001")
            if outcome:
                ET.SubElement(case, outcome, message="synthetic refusal control")
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        ET.ElementTree(tree).write(path, encoding="utf-8", xml_declaration=True)
        return pin(path)

    def emit_capture(self, bindings_pin, *, kind="history"):
        directory = self.output / "capture"
        directory.mkdir()
        cases, processes = [], []
        for number, nodeid in enumerate(PACKET_IDS[kind]):
            parameter = nodeid.split("[", 1)[1][:-1]
            case, context = parameter.rsplit("-", 1)
            raw = directory / (str(number) + ".json")
            row = {"id": case, "context": context,
                   "raw": self.save(raw, {"fixture": "synthetic transport, not UI acceptance"})}
            if context == "imgui_python":
                row["launcher_origin"] = self.save(directory / (str(number) + ".origin.json"),
                                                   {"fixture": "synthetic origin transport"})
            cases.append(row)
            process_pin = self.save(directory / (str(number) + ".process.json"), self.process())
            processes.append({"owned_process_receipt": process_pin})
        capture = {"state": "passed", "acceptance_claim": False,
                   "inputs": {"product_bindings": bindings_pin}, "cases": cases, "processes": processes}
        if kind in ("appearance", "drawing"):
            capture["kind_selection"] = kind
        self.save(directory / "capture.json", capture)
        self.save(directory / "provenance.json", {"fixture": "synthetic, not executed reader provenance"})
        return capture

    def make_packet(self, *, kind="history"):
        self.output.mkdir(parents=True)
        bindings_pin = self.save(self.output / "product-bindings.json", self.bindings(kind=kind))
        self.emit_capture(bindings_pin, kind=kind)
        row = {"schema": "imagejxx." + kind + "-suite.001", "state": "passed", "acceptance_claim": False,
               "product_bindings": bindings_pin,
               "capture": pin(self.output / "capture/capture.json"),
               "provenance": pin(self.output / "capture/provenance.json"),
               "reader_junit": self.write_junit(self.output / "reader/pytest.xml", PACKET_IDS[kind]),
               "capture_process": self.save(self.output / "capture-owner/process.json", self.process()),
               "reader_process": self.save(self.output / "reader/process.json", self.process())}
        self.save(self.output / "receipt.json", row)
        return row

    def validate(self, *, kind="history"):
        options = {} if kind == "history" else {"kind": kind}
        return suite.validate_history_output(self.output, self.root, self.application, self.modules, **options)

    def rebind_capture(self, change):
        path = self.output / "capture/capture.json"
        capture = self.read(path)
        change(capture)
        row = self.read(self.output / "receipt.json")
        row["capture"] = self.save(path, capture)
        self.save(self.output / "receipt.json", row)

    def rebind_receipt_member(self, name, change):
        row = self.read(self.output / "receipt.json")
        path = Path(row[name]["path"])
        value = self.read(path)
        change(value)
        row[name] = self.save(path, value)
        self.save(self.output / "receipt.json", row)

    def imported_module(self, name):
        if name == "numpy":
            return self.numpy
        if name not in self.module_objects:
            raise AssertionError("Unexpected product import: " + name)
        return self.module_objects[name]

    def test_literal_eight_ids_and_complete_current_packet_admit_only_original_env(self):
        self.assertEqual(suite.HISTORY_IDS, HISTORY_IDS)
        self.make_packet()
        self.assertEqual(self.validate(), {
            "IMAGEJXX_HISTORY_SETTINGS_UI_CASE_ROOT": str(self.output / "capture")})

    def test_authentic_build_contract_checks_terminal_owner_and_sources(self):
        self.assertEqual(suite.history_build_inputs(self.build_path, self.root), self.build_document)
        for role, final in self.build_document["built_products"].items():
            self.assertNotEqual(final["sha256"], self.build_document["raw_cache_products"][role]["sha256"])

    def test_failed_or_wrong_build_envelope_refuses_even_with_fresh_hashes(self):
        for field, value in (("kind", "unrelated-build"), ("state", "failed"),
                             ("schema_version", 2), ("acceptance", True)):
            original = deepcopy(self.build_document)
            with self.subTest(field=field):
                self.build_document[field] = value
                self.write_build()
                with self.assertRaises((ValueError, RuntimeError)):
                    suite.history_build_inputs(self.build_path, self.root)
            self.build_document = original

    def test_rebound_terminal_owner_semantic_contradictions_refuse(self):
        variants = ({"exit_code": 7}, {"cleanup_complete": False}, {"target_completed": False},
                    {"state": "timed_out"}, {"timed_out": True}, {"argv": ["other"]},
                    {"cwd": str(self.root.parent)}, {"helper": {**self.helper_pin, "sha256": "0" * 64}})
        for change in variants:
            original = deepcopy(self.build_process)
            with self.subTest(change=change):
                self.build_process.update(change)
                self.write_build()
                with self.assertRaises((ValueError, RuntimeError)):
                    suite.history_build_inputs(self.build_path, self.root)
            self.build_process = original

    def test_missing_duplicate_or_foreign_history_source_membership_refuses(self):
        for alteration in ("missing", "duplicate", "foreign"):
            original = deepcopy(self.freeze_document)
            with self.subTest(alteration=alteration):
                if alteration == "missing":
                    self.freeze_document["sources"].pop()
                elif alteration == "duplicate":
                    self.freeze_document["sources"].append(self.freeze_document["sources"][0])
                else:
                    self.freeze_document["repository"] = str(self.root.parent)
                self.write_build()
                with self.assertRaises((ValueError, RuntimeError)):
                    suite.history_build_inputs(self.build_path, self.root)
            self.freeze_document = original

    def test_role_only_freeze_cannot_replace_full_tracked_and_mandatory_closure(self):
        self.freeze_document["sources"] = [row for row in self.freeze_document["sources"]
                                            if row["logical_path"] in HISTORY_ROLES]
        self.assertEqual({row["logical_path"] for row in self.freeze_document["sources"]}, set(HISTORY_ROLES))
        self.write_build()
        with self.assertRaisesRegex(ValueError, "membership changed"):
            suite.history_build_inputs(self.build_path, self.root)

    def test_coherent_dry_run_receipt_cannot_authenticate_preexisting_products(self):
        self.build_document["argv"] = self.argv + ["--dry-run"]
        self.build_process["argv"] = list(self.build_document["argv"])
        self.write_build()
        self.assertTrue(all(Path(row["path"]).is_file()
                            for row in self.build_document["raw_cache_products"].values()))
        with self.assertRaisesRegex(ValueError, "configuration arguments"):
            suite.history_build_inputs(self.build_path, self.root)

    def test_rehashed_cache_must_still_be_complete_ninja_release_for_this_source(self):
        path = Path(self.build_document["cache"]["path"])
        original = path.read_text(encoding="utf-8")
        configuration = deepcopy(self.build_document["configuration"])
        variants = (("CMAKE_GENERATOR", "Ninja", "Unix Makefiles"),
                    ("CMAKE_BUILD_TYPE", "Release", "Debug"),
                    ("IMAGEJXX_BUILD_IO", "ON", "OFF"),
                    ("CMAKE_HOME_DIRECTORY", str(self.root), str(self.root.parent)))
        for key, before, after in variants:
            with self.subTest(key=key):
                lines = original.splitlines()
                line_index = next(index for index, line in enumerate(lines) if line.startswith(key + ":"))
                self.assertEqual(lines[line_index].split("=", 1)[1], before)
                lines[line_index] = lines[line_index].split("=", 1)[0] + "=" + after
                path.write_text("\n".join(lines) + "\n", encoding="utf-8")
                self.build_document["cache"] = pin(path)
                self.build_document["configuration"] = {**configuration, key: after}
                self.write_build()
                with self.assertRaisesRegex(ValueError, "Cache"):
                    suite.history_build_inputs(self.build_path, self.root)

    def test_source_mutation_refuses_without_rewriting_original_freeze(self):
        original = self.freeze_path.read_bytes()
        (self.root / "src/desktop/window.cpp").write_bytes(b"different source\n")
        with self.assertRaises((ValueError, RuntimeError)):
            suite.history_build_inputs(self.build_path, self.root)
        self.assertEqual(self.freeze_path.read_bytes(), original)

    def test_build_requires_exact_three_compiled_products(self):
        del self.build_document["built_products"]["_desktop"]
        self.write_build()
        with self.assertRaises((ValueError, RuntimeError)):
            suite.history_build_inputs(self.build_path, self.root)

    def test_real_binding_logic_matches_installed_bytes_and_actual_module_paths(self):
        with patch.object(suite.importlib, "import_module", side_effect=self.imported_module):
            actual = suite.history_bindings(self.build_path, self.application, self.package, self.root)
        self.assertEqual(actual, self.bindings())

    def test_binding_refuses_installed_native_that_differs_from_frozen_product(self):
        # Keep the authenticated final product intact; the imported object now
        # exposes another in-package module, isolating the binding comparison.
        other = self.write("installed/site-packages/imagejxx/other-native.fixture",
                           b"different installed native\n")
        self.module_objects["imagejxx._native"].__file__ = str(other)
        with patch.object(suite.importlib, "import_module", side_effect=self.imported_module):
            with self.assertRaisesRegex(ValueError, "differs from the authenticated build"):
                suite.history_bindings(self.build_path, self.application, self.package, self.root)

    def test_packaging_metadata_byte_changes_refuse_original_pins(self):
        for name in ("METADATA", "direct_url.json", "RECORD"):
            with self.subTest(name=name):
                path = self.dist_info / name
                original = path.read_bytes()
                path.write_bytes(original + b"\n")
                with self.assertRaises((ValueError, RuntimeError)):
                    suite.history_build_inputs(self.build_path, self.root)
                path.write_bytes(original)

    def test_rehashed_metadata_and_record_cannot_claim_another_distribution_name(self):
        path = self.dist_info / "METADATA"
        path.write_bytes(b"Metadata-Version: 2.1\nName: other-package\nVersion: 0.2.0\n\n")
        logical = path.relative_to(self.site).as_posix()
        index = next(index for index, row in enumerate(self.record_rows) if row[0] == logical)
        self.record_rows[index] = self.record_row(logical, path)
        packaging = self.build_document["packaging"]
        packaging.update(metadata=pin(path), distribution_name="other-package", record=self.write_record())
        self.write_build()
        with self.assertRaisesRegex(ValueError, "does not identify imagejxx"):
            suite.history_build_inputs(self.build_path, self.root)

    def test_rebound_packaging_url_still_requires_actual_source_root(self):
        path = self.dist_info / "direct_url.json"
        wrong_url = self.root.parent.as_uri()
        path.write_text(json.dumps({"url": wrong_url, "dir_info": {}}) + "\n", encoding="utf-8")
        logical = path.relative_to(self.site).as_posix()
        index = next(index for index, row in enumerate(self.record_rows) if row[0] == logical)
        self.record_rows[index] = self.record_row(logical, path)
        packaging = self.build_document["packaging"]
        packaging.update(direct_url=pin(path), source_url=wrong_url, record=self.write_record())
        self.write_build()
        with self.assertRaisesRegex(ValueError, "direct URL.*source root"):
            suite.history_build_inputs(self.build_path, self.root)

    def test_rebound_product_record_hash_or_size_does_not_replace_actual_final_bytes(self):
        original_rows = deepcopy(self.record_rows)
        original_packaging = deepcopy(self.build_document["packaging"])
        for field, column, value in (("record_hash", 1, "sha256=" + "A" * 43),
                                      ("record_size", 2, 1)):
            with self.subTest(field=field):
                self.record_rows = deepcopy(original_rows)
                self.build_document["packaging"] = deepcopy(original_packaging)
                packaging = self.build_document["packaging"]
                selected = packaging["products"]["_native"]
                index = next(index for index, row in enumerate(self.record_rows)
                             if row[0] == selected["record_path"])
                self.record_rows[index][column] = str(value)
                selected[field] = value
                packaging["record"] = self.write_record()
                self.write_build()
                with self.assertRaisesRegex(ValueError, "original RECORD hash/size"):
                    suite.history_build_inputs(self.build_path, self.root)

    def test_rebound_record_path_cannot_redirect_equal_bytes_to_another_product(self):
        final = self.build_document["built_products"]["_desktop"]
        other = self.write("installed/site-packages/other/desktop.fixture", Path(final["path"]).read_bytes())
        packaging = self.build_document["packaging"]
        selected = packaging["products"]["_desktop"]
        index = next(index for index, row in enumerate(self.record_rows)
                     if row[0] == selected["record_path"])
        logical = other.relative_to(self.site).as_posix()
        self.record_rows[index] = self.record_row(logical, other)
        selected["record_path"] = logical
        packaging["record"] = self.write_record()
        self.write_build()
        with self.assertRaisesRegex(ValueError, "original RECORD"):
            suite.history_build_inputs(self.build_path, self.root)

    def test_binding_refuses_foreign_module_or_unpinned_numpy(self):
        foreign = self.write("outside/app.py", b"foreign source bootstrap\n")
        original = self.module_objects["imagejxx.ui.app"].__file__
        self.module_objects["imagejxx.ui.app"].__file__ = str(foreign)
        with patch.object(suite.importlib, "import_module", side_effect=self.imported_module):
            with self.assertRaisesRegex(ValueError, "outside the installed package"):
                suite.history_bindings(self.build_path, self.application, self.package, self.root)
            self.module_objects["imagejxx.ui.app"].__file__ = original
            self.numpy.__version__ = "2.2.5"
            with self.assertRaisesRegex(ValueError, "pinned NumPy"):
                suite.history_bindings(self.build_path, self.application, self.package, self.root)

    def test_same_count_capture_with_wrong_original_id_refuses(self):
        self.make_packet()
        self.rebind_capture(lambda value: value["cases"][0].update(id="HU99"))
        with self.assertRaisesRegex(ValueError, "original complete paired membership"):
            self.validate()

    def test_raw_capture_or_launcher_mutation_is_not_hidden_by_unchanged_count(self):
        for member in ("raw", "launcher_origin"):
            with self.subTest(member=member):
                if not self.output.exists():
                    self.make_packet()
                capture = self.read(self.output / "capture/capture.json")
                row = next(row for row in capture["cases"] if member in row)
                path = Path(row[member]["path"])
                original = path.read_bytes()
                path.write_bytes(b"changed original observation\n")
                with self.assertRaises((ValueError, RuntimeError)):
                    self.validate()
                path.write_bytes(original)

    def test_current_application_or_module_identity_must_match_packet(self):
        self.make_packet()
        other = self.write("installed/other-app", b"same fixture is not the same installed path\n")
        with self.assertRaisesRegex(ValueError, "different installed products"):
            suite.validate_history_output(self.output, self.root, other, self.modules)
        modules = deepcopy(self.modules)
        modules["imagejxx._native"] = pin(other)
        with self.assertRaisesRegex(ValueError, "different installed products"):
            suite.validate_history_output(self.output, self.root, self.application, modules)

    def test_each_producer_requires_resolved_ownership(self):
        self.make_packet()
        def change(capture):
            row = capture["processes"][3]
            proof = self.read(row["owned_process_receipt"]["path"])
            proof["cleanup_complete"] = False
            row["owned_process_receipt"] = self.save(row["owned_process_receipt"]["path"], proof)
        self.rebind_capture(change)
        with self.assertRaisesRegex(ValueError, "producer ownership"):
            self.validate()

    def test_missing_producer_receipt_refuses_even_when_all_eight_raws_remain(self):
        self.make_packet()
        self.rebind_capture(lambda value: value["processes"].pop())
        with self.assertRaisesRegex(ValueError, "eight resolved producers"):
            self.validate()

    def test_junit_requires_exact_ids_order_and_no_skip_failure_or_error(self):
        self.make_packet()
        variants = [(HISTORY_IDS[:-1], None), (tuple(reversed(HISTORY_IDS)), None),
                    (HISTORY_IDS[:-1] + (HISTORY_IDS[0],), None)]
        variants.extend((HISTORY_IDS, outcome) for outcome in ("skipped", "failure", "error"))
        for ids, outcome in variants:
            with self.subTest(ids=ids, outcome=outcome):
                row = self.read(self.output / "receipt.json")
                row["reader_junit"] = self.write_junit(self.output / "reader/pytest.xml", ids, outcome)
                self.save(self.output / "receipt.json", row)
                with self.assertRaisesRegex(ValueError, "unchanged eight passing reader identities"):
                    self.validate()

    def test_capture_and_reader_stage_owners_each_must_be_resolved(self):
        self.make_packet()
        for name in ("capture_process", "reader_process"):
            original = self.read(self.output / "receipt.json")
            original_proof = self.read(original[name]["path"])
            with self.subTest(name=name):
                self.rebind_receipt_member(name, lambda proof: proof.update(target_completed=False))
                with self.assertRaisesRegex(ValueError, "packet process ownership"):
                    self.validate()
            self.save(original[name]["path"], original_proof)
            self.save(self.output / "receipt.json", original)

    def smoke(self, output, root, environment):
        self.events.append("smoke")
        self.assertEqual(root, self.root)
        self.assertFalse(Path(output).exists())
        self.assertEqual(environment["IMAGEJXX_REQUIRE_RENDER_TESTS"], "1")
        if self.fail_stage == "smoke":
            raise ValueError("synthetic smoke failure")
        return {"fixture": "synthetic smoke only", "state": "passed"}

    def get_bindings(self, build_receipt, application, package, root, *, kind="history"):
        self.events.append("bindings")
        self.assertEqual((build_receipt, application, package, root),
                         (self.build_path, self.application, self.package, self.root))
        if self.fail_stage == "bindings":
            raise ValueError("synthetic binding failure")
        return self.bindings(kind=kind)

    def command(self, argv, *, output, root, environment, timeout, **kwargs):
        stage = "capture" if "--kind" in argv else "reader"
        kind = argv[argv.index("--kind") + 1] if stage == "capture" else (
            "drawing" if DRAWING_ENV in environment else "appearance" if APPEARANCE_ENV in environment else "history")
        self.events.append(stage)
        self.commands.append((stage, list(argv), deepcopy(environment), timeout))
        self.assertEqual(root, self.root)
        self.assertEqual(environment["PYTEST_DISABLE_PLUGIN_AUTOLOAD"], "1")
        if self.fail_stage == stage + "-missing-receipt":
            if stage == "capture":
                self.emit_capture(pin(self.output / "product-bindings.json"), kind=kind)
            raise RuntimeError("synthetic " + stage + " started with missing process receipt")
        proof = self.process(argv)
        if self.fail_stage == stage:
            proof.update(exit_code=19)
        elif self.fail_stage == stage + "-unresolved":
            proof.update(cleanup_complete=False)
        proof.update(self.proof_override)
        self.save(Path(output) / "process.json", proof)
        if stage == "capture" and self.fail_stage not in ("capture", "capture-unresolved"):
            self.emit_capture(pin(self.output / "product-bindings.json"), kind=kind)
        if stage == "reader":
            self.write_junit(Path(output) / "pytest.xml", PACKET_IDS[kind])
        return proof

    def run_suite(self, *, kind="history"):
        with patch.object(suite.importlib.metadata, "distribution", side_effect=self.installed_distribution), \
                patch.object(suite, "installed_application", return_value=self.application), \
                patch.object(suite, "history_bindings", side_effect=self.get_bindings), \
                patch.object(suite, "installed_smoke", side_effect=self.smoke), \
                patch.object(suite, "complete_command", side_effect=self.command):
            options = {} if kind == "history" else {"kind": kind}
            return suite.run_history_suite(self.output, self.root, self.build_path, **options)

    def test_orchestration_binds_smokes_captures_then_runs_exact_original_readers(self):
        self.run_suite()
        self.assertEqual(self.events, ["bindings", "smoke", "capture", "reader"])
        row = self.read(self.output / "receipt.json")
        self.assertEqual(row["state"], "passed")
        self.assertIs(row["acceptance_claim"], False)
        self.assertTrue(Path(row["capture_archive"]["path"]).is_file())
        self.assertEqual(self.validate(), {
            "IMAGEJXX_HISTORY_SETTINGS_UI_CASE_ROOT": str(self.output / "capture")})
        capture_argv = self.commands[0][1]
        self.assertEqual(capture_argv[:6], [sys.executable, "-I", "-X", "utf8", "-B",
                                          str(self.root / "scripts/capture_shortcuts_history_ui.py")])
        self.assertEqual(capture_argv[capture_argv.index("--kind") + 1], "history")
        self.assertEqual(capture_argv[capture_argv.index("--source-freeze") + 1], str(self.freeze_path))
        self.assertEqual(self.commands[0][3], 90)
        reader_argv = self.commands[1][1]
        first = reader_argv.index("pytest") + 1
        self.assertEqual(tuple(reader_argv[first:first + 8]), HISTORY_IDS)
        self.assertIn("no:cacheprovider", reader_argv)
        self.assertEqual(self.commands[1][3], 120)
        self.assertNotIn("IMAGEJXX_HISTORY_SETTINGS_UI_CASE_ROOT", self.commands[0][2])
        self.assertEqual(self.commands[1][2]["IMAGEJXX_HISTORY_SETTINGS_UI_CASE_ROOT"],
                         str(self.output / "capture"))

    def test_failed_setup_never_starts_capture_or_reader(self):
        for stage, expected in (("bindings", ["bindings"]), ("smoke", ["bindings", "smoke"])):
            with self.subTest(stage=stage):
                self.output = self.root / ("build/failure-" + stage)
                self.events = []
                self.fail_stage = stage
                with self.assertRaisesRegex(ValueError, "synthetic"):
                    self.run_suite()
                self.assertEqual(self.events, expected)
                self.assertEqual(self.read(self.output / "receipt.json")["state"], "failed")

    def test_failed_or_unresolved_capture_never_starts_reader(self):
        for stage in ("capture", "capture-unresolved"):
            with self.subTest(stage=stage):
                self.output = self.root / ("build/failure-" + stage)
                self.events = []
                self.fail_stage = stage
                with self.assertRaisesRegex(ValueError, "history capture failed"):
                    self.run_suite()
                self.assertEqual(self.events, ["bindings", "smoke", "capture"])
                self.assertEqual(self.read(self.output / "receipt.json")["state"], "failed")

    def test_failed_or_unresolved_reader_keeps_failed_receipt_and_raw_capture(self):
        for stage in ("reader", "reader-unresolved"):
            with self.subTest(stage=stage):
                self.output = self.root / ("build/failure-" + stage)
                self.events = []
                self.fail_stage = stage
                with self.assertRaisesRegex(ValueError, "history reader failed"):
                    self.run_suite()
                self.assertEqual(self.events, ["bindings", "smoke", "capture", "reader"])
                self.assertEqual(self.read(self.output / "receipt.json")["state"], "failed")
                self.assertTrue((self.output / "capture/capture.json").is_file())
                if stage == "reader-unresolved":
                    self.assertFalse((self.output / "capture.zip").exists())
                    self.assertNotIn("capture_archive", self.read(self.output / "receipt.json"))
                with self.assertRaisesRegex(ValueError, "has not passed"):
                    self.validate()

    def test_started_stage_without_process_receipt_keeps_raws_and_withholds_archive(self):
        for stage in ("reader", "capture"):
            with self.subTest(stage=stage):
                self.output = self.root / ("build/missing-receipt-" + stage)
                self.events = []
                self.fail_stage = stage + "-missing-receipt"
                with self.assertRaisesRegex(RuntimeError, "started with missing process receipt"):
                    self.run_suite()
                expected = ["bindings", "smoke", "capture"] + (["reader"] if stage == "reader" else [])
                self.assertEqual(self.events, expected)
                receipt = self.read(self.output / "receipt.json")
                self.assertEqual(receipt["state"], "failed")
                self.assertEqual(receipt["error"]["type"], "RuntimeError")
                self.assertIn("started with missing process receipt", receipt["error"]["message"])
                directory = "reader" if stage == "reader" else "capture-owner"
                self.assertFalse((self.output / directory / "process.json").exists())
                self.assertTrue((self.output / "capture/capture.json").is_file())
                self.assertEqual(len(self.read(self.output / "capture/capture.json")["cases"]), 8)
                self.assertFalse((self.output / "capture.zip").exists())
                self.assertNotIn("capture_archive", receipt)
                with self.assertRaisesRegex(ValueError, "has not passed"):
                    self.validate()

    def test_history_suite_refuses_preexisting_output_without_replacing_it(self):
        self.output.mkdir(parents=True)
        sentinel = self.output / "keep.txt"
        sentinel.write_bytes(b"keep caller bytes\n")
        with self.assertRaisesRegex(ValueError, "fresh owned"):
            self.run_suite()
        self.assertEqual(sentinel.read_bytes(), b"keep caller bytes\n")
        self.assertEqual(self.events, [])

    def test_assertion_and_pytest_environment_overrides_refuse_before_binding(self):
        for key in ("PYTHONOPTIMIZE", "PYTEST_ADDOPTS", "PYTEST_PLUGINS"):
            with self.subTest(key=key), patch.dict(os.environ, {key: "unexpected"}):
                self.output = self.root / ("build/forbidden-" + key.lower())
                with self.assertRaisesRegex(ValueError, "original assertion and pytest profile"):
                    self.run_suite()
                self.assertEqual(self.events, [])

    def test_cli_history_entry_dispatches_only_the_bounded_history_suite(self):
        argv = ["ci_python_suite.py", "--history-suite", "--output", str(self.output),
                "--history-build-receipt", str(self.build_path), "--repository", str(self.root)]
        with patch.object(sys, "argv", argv), \
                patch.object(suite, "run_history_suite") as history, \
                patch.object(suite, "run") as complete, \
                patch.object(suite, "reference_readiness") as readiness:
            suite.main()
        history.assert_called_once_with(self.output, self.root, self.build_path)
        complete.assert_not_called()
        readiness.assert_not_called()

    def test_cli_complete_entry_passes_existing_history_output_without_replay(self):
        argv = ["ci_python_suite.py", "--output", str(self.root / "build/complete"),
                "--history-output", str(self.output)]
        with patch.object(sys, "argv", argv), \
                patch.object(suite, "run_history_suite") as history, \
                patch.object(suite, "run") as complete:
            suite.main()
        history.assert_not_called()
        complete.assert_called_once_with(self.root / "build/complete", ROOT, history_output=self.output)

    def test_cli_missing_build_or_cross_profile_receipt_is_refused_before_dispatch(self):
        variants = (["--history-suite", "--output", str(self.output)],
                    ["--output", str(self.output), "--history-build-receipt", str(self.build_path)],
                    ["--history-suite", "--output", str(self.output),
                     "--history-build-receipt", str(self.build_path), "--history-output", str(self.output)])
        for arguments in variants:
            with self.subTest(arguments=arguments), patch.object(sys, "argv", ["ci_python_suite.py", *arguments]), \
                    patch.object(suite, "run_history_suite") as history, \
                    patch.object(suite, "run") as complete, redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as caught:
                    suite.main()
                self.assertEqual(caught.exception.code, 2)
                history.assert_not_called()
                complete.assert_not_called()


class AppearanceIntegration(unittest.TestCase):
    """Reuse only the synthetic fixture, never inherit/count history tests twice."""

    def setUp(self):
        self.f = HistoryIntegration()
        self.f.setUp()
        self.addCleanup(self.f.doCleanups)
        f = self.f
        f.output = f.root / "build/appearance-suite"
        for logical in APPEARANCE_EXTRA_ROLES:
            f.write(logical, ("synthetic frozen appearance source: " + logical + "\n").encode())
        f.tracked.extend(APPEARANCE_EXTRA_ROLES)
        f.source_names = sorted(set(f.source_names) | set(APPEARANCE_EXTRA_ROLES))
        f.freeze_document["sources"] = [{"logical_path": n, **pin(f.root / n)} for n in f.source_names]
        f.start_patch(patch.object(builder, "_history_paths", return_value=set(HISTORY_ROLES + APPEARANCE_EXTRA_ROLES)))
        f.write_build()

    def test_literal_four_ids_admit_only_appearance_environment_without_replay(self):
        self.assertEqual(suite.HISTORY_IDS, HISTORY_IDS)
        self.assertEqual(suite.APPEARANCE_IDS, APPEARANCE_IDS)
        f = self.f
        f.make_packet(kind="appearance")
        with patch.object(suite, "complete_command", side_effect=AssertionError("No packet replay")), \
                patch.object(suite, "run_history_suite", side_effect=AssertionError("No producer replay")):
            self.assertEqual(f.validate(kind="appearance"), {APPEARANCE_ENV: str(f.output / "capture")})
        self.assertEqual(f.events, [])

    def test_real_binding_logic_preserves_actual_product_and_frozen_source_joins(self):
        f = self.f
        with patch.object(suite.importlib, "import_module", side_effect=f.imported_module):
            actual = suite.history_bindings(f.build_path, f.application, f.package, f.root, kind="appearance")
        self.assertEqual(actual, f.bindings(kind="appearance"))
        self.assertEqual(actual["kind_selection"], "appearance")
        self.assertEqual(actual["kind"], "c2.installed-ui-capture-bindings.001")

    def test_missing_appearance_role_refuses_before_any_product_import(self):
        f = self.f
        missing = "tests/python/settings_appearance_reader.py"
        f.tracked.remove(missing)
        roles = set(HISTORY_ROLES + APPEARANCE_EXTRA_ROLES) - {missing}
        f.freeze_document["sources"] = [r for r in f.freeze_document["sources"] if r["logical_path"] != missing]
        f.write_build()
        with patch.object(builder, "_history_paths", return_value=roles):
            self.assertEqual(suite.history_build_inputs(f.build_path, f.root), f.build_document)
            with patch.object(suite.importlib, "import_module", side_effect=AssertionError("No import before source admission")):
                with self.assertRaisesRegex(ValueError, "missing frozen source roles.*settings_appearance_reader"):
                    suite.history_bindings(f.build_path, f.application, f.package, f.root, kind="appearance")

    def test_missing_new_planar_driver_role_refuses_before_any_product_import(self):
        f = self.f
        missing = "src/desktop/path_segment_ui_probe.cpp"
        f.tracked.remove(missing)
        roles = set(HISTORY_ROLES + APPEARANCE_EXTRA_ROLES) - {missing}
        f.freeze_document["sources"] = [r for r in f.freeze_document["sources"] if r["logical_path"] != missing]
        f.write_build()
        with patch.object(builder, "_history_paths", return_value=roles):
            self.assertEqual(suite.history_build_inputs(f.build_path, f.root), f.build_document)
            with patch.object(suite.importlib, "import_module", side_effect=AssertionError("No import before source admission")):
                with self.assertRaisesRegex(ValueError, "missing frozen source roles.*path_segment_ui_probe"):
                    suite.history_bindings(f.build_path, f.application, f.package, f.root, kind="appearance")

    def test_history_and_appearance_packets_cannot_substitute_for_one_another(self):
        f = self.f
        f.make_packet(kind="appearance")
        with self.assertRaisesRegex(ValueError, "history packet has not passed"):
            f.validate()
        f.output = f.root / "build/original-history"
        f.make_packet()
        with self.assertRaisesRegex(ValueError, "appearance packet has not passed"):
            f.validate(kind="appearance")

    def test_four_capture_cases_require_exact_identity_order_and_owned_exits(self):
        f = self.f
        f.make_packet(kind="appearance")
        original = f.read(f.output / "capture/capture.json")
        variants = (
            lambda c: c["cases"][0].update(id="HU01"),
            lambda c: c["cases"].reverse(),
            lambda c: c["processes"].pop(),
            lambda c: c.update(kind_selection="history"),
        )
        for change in variants:
            f.rebind_capture(lambda c: (c.clear(), c.update(deepcopy(original)), change(c)))
            with self.subTest(change=change), self.assertRaises(ValueError):
                f.validate(kind="appearance")

    def test_four_reader_identities_refuse_same_count_wrong_ids_and_every_nonpass(self):
        f = self.f
        f.make_packet(kind="appearance")
        variants = [(APPEARANCE_IDS[:-1] + (HISTORY_IDS[0],), None),
                    (tuple(reversed(APPEARANCE_IDS)), None)]
        variants += [(APPEARANCE_IDS, outcome) for outcome in ("skipped", "failure", "error")]
        for ids, outcome in variants:
            with self.subTest(ids=ids, outcome=outcome):
                row = f.read(f.output / "receipt.json")
                row["reader_junit"] = f.write_junit(f.output / "reader/pytest.xml", ids, outcome)
                f.save(f.output / "receipt.json", row)
                with self.assertRaisesRegex(ValueError, "unchanged four passing reader identities"):
                    f.validate(kind="appearance")

    def test_appearance_orchestration_uses_same_owner_and_exact_four_reader_ids(self):
        f = self.f
        with patch.dict(os.environ, {APPEARANCE_ENV: "unvalidated inherited capture"}):
            f.run_suite(kind="appearance")
        self.assertEqual(f.events, ["bindings", "smoke", "capture", "reader"])
        receipt = f.read(f.output / "receipt.json")
        self.assertEqual(receipt["schema"], "imagejxx.appearance-suite.001")
        self.assertEqual(receipt["state"], "passed")
        self.assertIs(receipt["acceptance_claim"], False)
        self.assertEqual(receipt["started_stages"], ["capture-owner", "reader"])
        capture_argv = f.commands[0][1]
        self.assertEqual(capture_argv[capture_argv.index("--kind") + 1], "appearance")
        self.assertEqual(capture_argv[capture_argv.index("--source-freeze") + 1], str(f.freeze_path))
        self.assertEqual(f.commands[0][3], 330)
        self.assertNotIn(APPEARANCE_ENV, f.commands[0][2])
        reader_argv = f.commands[1][1]
        first = reader_argv.index("pytest") + 1
        self.assertEqual(tuple(reader_argv[first:first + 4]), APPEARANCE_IDS)
        self.assertEqual(f.commands[1][3], 120)
        self.assertEqual(f.commands[1][2][APPEARANCE_ENV], str(f.output / "capture"))
        self.assertTrue(Path(receipt["capture_archive"]["path"]).is_file())
        self.assertEqual(f.validate(kind="appearance"), {APPEARANCE_ENV: str(f.output / "capture")})

    def test_failed_appearance_setup_stops_before_capture(self):
        f = self.f
        for stage, expected in (("bindings", ["bindings"]), ("smoke", ["bindings", "smoke"])):
            with self.subTest(stage=stage):
                f.output = f.root / ("build/appearance-failed-" + stage)
                f.events = []
                f.fail_stage = stage
                with self.assertRaisesRegex(ValueError, "synthetic"):
                    f.run_suite(kind="appearance")
                self.assertEqual(f.events, expected)
                self.assertEqual(f.read(f.output / "receipt.json")["state"], "failed")

    def test_unresolved_or_missing_appearance_reader_keeps_raw_and_withholds_archive(self):
        f = self.f
        for stage in ("reader-unresolved", "reader-missing-receipt"):
            with self.subTest(stage=stage):
                f.output = f.root / ("build/appearance-" + stage)
                f.events = []
                f.fail_stage = stage
                with self.assertRaises((ValueError, RuntimeError)):
                    f.run_suite(kind="appearance")
                receipt = f.read(f.output / "receipt.json")
                self.assertEqual(receipt["state"], "failed")
                self.assertEqual(len(f.read(f.output / "capture/capture.json")["cases"]), 4)
                self.assertTrue((f.output / "capture/capture.json").is_file())
                self.assertFalse((f.output / "capture.zip").exists())
                self.assertNotIn("capture_archive", receipt)
                with self.assertRaisesRegex(ValueError, "has not passed"):
                    f.validate(kind="appearance")

    def test_unknown_kind_refuses_before_output_or_any_source_product_admission(self):
        f = self.f
        for kind in ("shortcuts", "", None):
            with self.subTest(kind=kind), patch.object(suite, "history_helper") as helper, \
                    patch.object(suite, "history_builder") as build:
                with self.assertRaisesRegex(ValueError, "fixed history, appearance or drawing"):
                    suite.run_history_suite(f.output, f.root, f.build_path, kind=kind)
                self.assertFalse(f.output.exists())
                helper.assert_not_called()
                build.assert_not_called()

    def test_cli_appearance_entry_is_separate_and_does_not_run_complete_or_readiness(self):
        f = self.f
        argv = ["ci_python_suite.py", "--appearance-suite", "--output", str(f.output),
                "--appearance-build-receipt", str(f.build_path), "--repository", str(f.root)]
        with patch.object(sys, "argv", argv), patch.object(suite, "run_history_suite") as packet, \
                patch.object(suite, "run") as complete, patch.object(suite, "reference_readiness") as readiness:
            suite.main()
        packet.assert_called_once_with(f.output, f.root, f.build_path, kind="appearance")
        complete.assert_not_called()
        readiness.assert_not_called()

    def test_cli_complete_admits_both_existing_packets_without_capture_dispatch(self):
        f = self.f
        previous_history = f.root / "build/qualified-history"
        complete_output = f.root / "build/complete"
        argv = ["ci_python_suite.py", "--output", str(complete_output),
                "--history-output", str(previous_history), "--appearance-output", str(f.output)]
        with patch.object(sys, "argv", argv), patch.object(suite, "run_history_suite") as packet, \
                patch.object(suite, "run") as complete:
            suite.main()
        packet.assert_not_called()
        complete.assert_called_once_with(complete_output, ROOT, history_output=previous_history,
                                         appearance_output=f.output)

    def test_cli_missing_build_and_cross_profile_arguments_refuse_before_dispatch(self):
        f = self.f
        variants = (
            ["--appearance-suite", "--output", str(f.output)],
            ["--appearance-suite", "--output", str(f.output), "--history-build-receipt", str(f.build_path)],
            ["--appearance-suite", "--output", str(f.output), "--appearance-build-receipt", str(f.build_path),
             "--appearance-output", str(f.output)],
            ["--history-suite", "--output", str(f.output), "--history-build-receipt", str(f.build_path),
             "--appearance-build-receipt", str(f.build_path)],
            ["--output", str(f.output), "--appearance-build-receipt", str(f.build_path)],
            ["--headless-suite", "--output", str(f.output), "--appearance-output", str(f.output)],
        )
        for arguments in variants:
            with self.subTest(arguments=arguments), patch.object(sys, "argv", ["ci_python_suite.py", *arguments]), \
                    patch.object(suite, "run_history_suite") as packet, patch.object(suite, "run") as complete, \
                    redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as caught:
                    suite.main()
                self.assertEqual(caught.exception.code, 2)
                packet.assert_not_called()
                complete.assert_not_called()


class DrawingIntegration(unittest.TestCase):
    """Actual metadata joins for C3, with explicitly fake installed products."""
    def setUp(self):
        self.f = HistoryIntegration()
        self.f.setUp()
        self.addCleanup(self.f.doCleanups)
        f = self.f
        f.output = f.root / "build/drawing-suite"
        extra = set(APPEARANCE_EXTRA_ROLES + DRAWING_EXTRA_ROLES)
        for name in extra:
            if not (f.root / name).exists():
                f.write(name, ("synthetic frozen reader/consumer role: " + name + "\n").encode())
        f.tracked.extend(extra - set(f.tracked))
        f.source_names = sorted(set(f.source_names) | extra)
        f.freeze_document["sources"] = [{"logical_path": n, **pin(f.root / n)} for n in f.source_names]
        f.start_patch(patch.object(builder, "_history_paths", return_value=set(HISTORY_ROLES) | extra))
        f.write_build()

    def test_four_literal_ids_and_only_drawing_environment_are_admitted(self):
        self.assertEqual(suite.DRAWING_IDS, DRAWING_IDS)
        f = self.f
        f.make_packet(kind="drawing")
        with patch.object(suite, "complete_command", side_effect=AssertionError("No replay")):
            self.assertEqual(f.validate(kind="drawing"), {DRAWING_ENV: str(f.output / "capture")})
        for kind in ("history", "appearance"):
            with self.assertRaisesRegex(ValueError, "has not passed"):
                f.validate(kind=kind)
        self.assertEqual(f.events, [])

    def test_real_binding_and_ordered_parent_keep_c3_contract(self):
        f = self.f
        with patch.object(suite.importlib, "import_module", side_effect=f.imported_module):
            value = suite.history_bindings(f.build_path, f.application, f.package, f.root, kind="drawing")
        self.assertEqual(value, f.bindings(kind="drawing"))
        self.assertEqual(value["kind"], "c3.installed-ui-capture-bindings.001")
        f.run_suite(kind="drawing")
        self.assertEqual(f.events, ["bindings", "smoke", "capture", "reader"])
        self.assertEqual(f.validate(kind="drawing"), {DRAWING_ENV: str(f.output / "capture")})
        reader = f.commands[-1]
        self.assertTrue(set(DRAWING_IDS) <= set(reader[1]))

    def test_missing_independent_reference_refuses_before_product_import(self):
        f = self.f
        missing = "tests/native/rounded_path_reference.inc"
        f.tracked.remove(missing)
        f.freeze_document["sources"] = [row for row in f.freeze_document["sources"] if row["logical_path"] != missing]
        f.write_build()
        roles = set(HISTORY_ROLES + APPEARANCE_EXTRA_ROLES + DRAWING_EXTRA_ROLES) - {missing}
        with patch.object(builder, "_history_paths", return_value=roles), \
                patch.object(suite.importlib, "import_module", side_effect=AssertionError("No import")):
            with self.assertRaisesRegex(ValueError, "Drawing build is missing frozen source roles.*rounded_path_reference"):
                suite.history_bindings(f.build_path, f.application, f.package, f.root, kind="drawing")

    def test_same_count_reader_substitution_is_rejected(self):
        f = self.f
        row = f.make_packet(kind="drawing")
        ids = list(DRAWING_IDS)
        ids[-1] = ids[-1].replace("drawing-reopen", "unreviewed-recipe")
        row["reader_junit"] = f.write_junit(f.output / "reader/pytest.xml", ids)
        f.save(f.output / "receipt.json", row)
        with self.assertRaises(ValueError):
            f.validate(kind="drawing")

    def test_cli_dispatches_only_drawing_and_requires_its_build_input(self):
        f = self.f
        argv = ["ci_python_suite.py", "--drawing-suite", "--output", str(f.output),
                "--history-build-receipt", str(f.build_path), "--repository", str(f.root)]
        with patch.object(sys, "argv", argv), patch.object(suite, "run_history_suite") as packet, \
                patch.object(suite, "run") as complete, patch.object(suite, "reference_readiness") as readiness:
            suite.main()
        packet.assert_called_once_with(f.output, f.root, f.build_path, kind="drawing")
        complete.assert_not_called()
        readiness.assert_not_called()
        with patch.object(sys, "argv", ["ci_python_suite.py", "--drawing-suite", "--output", str(f.output)]), \
                redirect_stderr(io.StringIO()), patch.object(suite, "run_history_suite") as packet:
            with self.assertRaises(SystemExit):
                suite.main()
            packet.assert_not_called()


class WindowsGlIntegration(unittest.TestCase):
    """One synthetic provider lifetime; original packet/file admission is real."""

    def setUp(self):
        fixture = DrawingIntegration()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        self.f = fixture.f
        self.output = self.f.root / "build/ci-python"
        self.history = self.f.root / "build/ci-history"
        self.appearance = self.f.root / "build/ci-appearance"
        self.drawing = self.f.root / "build/ci-drawing"
        self.provider_output = self.f.root / "build/ci-windows-gl"
        self.journal_path = self.provider_output / "suite.json"
        self.stage = {"fixture": "synthetic admitted stage; no DLLs or GL execution"}
        self.stage_pin = self.f.save(self.provider_output / "stage.json", self.stage)
        self.events = []
        self.failure = None
        self.change_raw = False
        self.real_stage = suite.windows_gl_stage
        provider = SimpleNamespace(verify_c2_runtime=self.runtime)
        self.stage_call = self.f.start_patch(patch.object(
            suite, "windows_gl_stage", return_value=(provider, self.stage, self.stage_pin)))
        self.f.start_patch(patch.object(suite, "run_history_suite", side_effect=self.packet))
        self.f.start_patch(patch.object(suite, "installed_application", return_value=self.f.application))
        self.f.start_patch(patch.object(suite, "run", side_effect=self.complete))
        # The actual lossless decoder is loaded from the reviewed test source;
        # only the synthetic checkout's placeholder location is redirected.
        decoder = suite._source_module("ci_gl_transport_control", ROOT / "tests/python/actual_observation_transport.py")
        def source_module(name, path):
            self.assertEqual((name, path), ("ci_gl_actual_transport", self.f.root / "tests/python/actual_observation_transport.py"))
            return decoder
        self.f.start_patch(patch.object(suite, "_source_module", side_effect=source_module))

    def journal(self):
        return self.f.read(self.journal_path)

    def invoke(self, *, output=None, history=None, appearance=None, drawing=None):
        return suite.run_windows_gl_suite(output or self.output, self.f.root, self.f.build_path,
                                          history or self.history, appearance or self.appearance, drawing or self.drawing)

    def packet(self, destination, root, build_receipt, *, kind):
        self.assertEqual((root, build_receipt), (self.f.root, self.f.build_path))
        self.assertEqual(destination, {"history": self.history, "appearance": self.appearance, "drawing": self.drawing}[kind])
        prior = {"history": [], "appearance": ["history"], "drawing": ["history", "appearance"]}[kind]
        self.assertEqual([row["kind"] for row in self.journal()["stages"]], prior)
        self.assertEqual(self.journal()["state"], "running")
        self.events.append(kind)
        if self.failure == kind:
            self.f.save(destination / "receipt.json", {"state": "failed", "fixture": "synthetic " + kind + " failure"})
            raise RuntimeError("synthetic " + kind + " failure")
        self.f.output = destination
        row = self.f.make_packet(kind=kind)
        capture = self.f.read(destination / "capture/capture.json")
        for case in capture["cases"]:
            raw = {"case_id": case["id"], "context": case["context"],
                   "fixture": "synthetic decoded transport; not scientific acceptance"}
            case["raw"] = self.f.save(case["raw"]["path"], raw)
        row["capture"] = self.f.save(destination / "capture/capture.json", capture)
        row["provenance"] = self.f.save(destination / "capture/provenance.json", {"cases": capture["cases"]})
        self.f.save(destination / "receipt.json", row)
        if kind == "appearance" and self.change_raw:
            Path(capture["cases"][0]["raw"]["path"]).write_bytes(b"changed after the pinned packet\n")

    def runtime(self, stage, reports):
        self.assertEqual(self.events, ["history", "appearance", "drawing"])
        self.events.append("runtime")
        self.assertEqual(stage, self.stage)
        self.assertEqual([(row["case_id"], row["context"]) for row in reports],
                         [(case, context) for case in ("appearance-persist", "appearance-reopen")
                          for context in ("imgui_cpp", "imgui_python")])
        self.assertEqual([row["kind"] for row in self.journal()["stages"]], ["history", "appearance", "drawing"])
        self.assertFalse((self.provider_output / "runtime-proof.json").exists())
        if self.failure == "runtime":
            raise ValueError("synthetic provider runtime failure")
        return {"schema": "imagejxx.windows-gl-runtime.001", "state": "verified",
                "fixture": "synthetic provider-proof callback; not actual GL qualification"}

    def complete(self, output, root, *, history_output, appearance_output, drawing_output):
        self.assertEqual(self.events, ["history", "appearance", "drawing", "runtime"])
        self.events.append("complete")
        self.assertEqual((output, root, history_output, appearance_output, drawing_output),
                         (self.output, self.f.root, self.history, self.appearance, self.drawing))
        journal = self.journal()
        self.assertEqual(journal["stages"][-1], {"kind": "complete", "state": "running"})
        self.assertEqual(journal["runtime_proof"], pin(self.provider_output / "runtime-proof.json"))
        # Complete receives the original qualified packet paths without replay.
        for directory, kind, environment in (
                (history_output, "history", "IMAGEJXX_HISTORY_SETTINGS_UI_CASE_ROOT"),
                (appearance_output, "appearance", APPEARANCE_ENV),
                (drawing_output, "drawing", DRAWING_ENV)):
            self.assertEqual(suite.validate_history_output(directory, root, self.f.application,
                                                          self.f.modules, kind=kind),
                             {environment: str(directory / "capture")})
        self.f.save(output / "receipt.json", {"state": "failed" if self.failure == "complete" else "passed",
                                             "fixture": "synthetic full-suite transport"})
        if self.failure == "complete":
            raise RuntimeError("synthetic complete failure")

    def test_paired_packets_then_runtime_proof_then_full_have_real_ordered_journal(self):
        self.invoke()
        self.assertEqual(self.events, ["history", "appearance", "drawing", "runtime", "complete"])
        row = self.journal()
        self.assertEqual((row["schema"], row["state"], row["acceptance_claim"]),
                         ("imagejxx.windows-gl-suite.001", "passed", False))
        self.assertEqual(row["stages"], [{"kind": kind, "state": "passed"} for kind in ("history", "appearance", "drawing", "complete")])
        self.assertEqual(row["stage"], self.stage_pin)
        proof = self.f.read(self.provider_output / "runtime-proof.json")
        self.assertEqual(proof["stage"], self.stage_pin)
        self.assertEqual(proof["runtime"]["state"], "verified")
        self.assertEqual(pin(self.provider_output / "stage.json"), self.stage_pin)

    def test_history_failure_keeps_appearance_and_runtime_diagnostics_but_blocks_full(self):
        self.failure = "history"
        with self.assertRaisesRegex(ValueError, "paired UI qualification failed"):
            self.invoke()
        self.assertEqual(self.events, ["history", "appearance", "drawing", "runtime"])
        row = self.journal()
        self.assertEqual([(r["kind"], r["state"]) for r in row["stages"]],
                         [("history", "failed"), ("appearance", "passed"), ("drawing", "passed"), ("complete", "not_run")])
        self.assertIn("synthetic history failure", row["stages"][0]["error"]["message"])
        self.assertEqual(row["runtime_proof"], pin(self.provider_output / "runtime-proof.json"))
        self.assertEqual(self.f.read(self.history / "receipt.json")["state"], "failed")
        self.assertEqual(row["state"], "failed")
        self.assertFalse(self.output.exists())

    def test_appearance_failure_blocks_runtime_and_full_without_hiding_history(self):
        self.failure = "appearance"
        with self.assertRaisesRegex(ValueError, "paired UI qualification failed"):
            self.invoke()
        self.assertEqual(self.events, ["history", "appearance", "drawing"])
        row = self.journal()
        self.assertEqual([(r["kind"], r["state"]) for r in row["stages"]],
                         [("history", "passed"), ("appearance", "failed"), ("drawing", "passed"), ("complete", "not_run")])
        self.assertEqual(row["runtime_proof"], {"state": "unavailable", "reason": "appearance packet failed"})
        self.assertEqual(row["state"], "failed")
        self.assertFalse(self.output.exists() or (self.provider_output / "runtime-proof.json").exists())

    def test_drawing_failure_preserves_appearance_runtime_proof_but_blocks_full(self):
        self.failure = "drawing"
        with self.assertRaisesRegex(ValueError, "paired UI qualification failed"):
            self.invoke()
        self.assertEqual(self.events, ["history", "appearance", "drawing", "runtime"])
        row = self.journal()
        self.assertEqual([(r["kind"], r["state"]) for r in row["stages"]],
                         [("history", "passed"), ("appearance", "passed"),
                          ("drawing", "failed"), ("complete", "not_run")])
        self.assertEqual(row["runtime_proof"], pin(self.provider_output / "runtime-proof.json"))
        self.assertFalse(self.output.exists())

    def test_provider_runtime_failure_is_retained_and_blocks_full(self):
        self.failure = "runtime"
        with self.assertRaisesRegex(ValueError, "synthetic provider runtime failure"):
            self.invoke()
        self.assertEqual(self.events, ["history", "appearance", "drawing", "runtime"])
        row = self.journal()
        self.assertEqual((row["state"], row["stages"][-1]["state"]), ("failed", "not_run"))
        self.assertIn("synthetic provider runtime failure", row["error"]["message"])
        self.assertNotIn("runtime_proof", row)
        self.assertFalse(self.output.exists())

    def test_full_failure_keeps_runtime_proof_and_failed_full_stage(self):
        self.failure = "complete"
        with self.assertRaisesRegex(RuntimeError, "synthetic complete failure"):
            self.invoke()
        self.assertEqual(self.events, ["history", "appearance", "drawing", "runtime", "complete"])
        row = self.journal()
        self.assertEqual((row["state"], row["stages"][-1]["state"]), ("failed", "failed"))
        self.assertIn("synthetic complete failure", row["stages"][-1]["error"]["message"])
        self.assertEqual(row["runtime_proof"], pin(self.provider_output / "runtime-proof.json"))
        self.assertEqual(self.f.read(self.output / "receipt.json")["state"], "failed")

    def test_only_exact_maintained_output_roots_are_admitted(self):
        for argument in ("output", "history", "appearance", "drawing"):
            with self.subTest(argument=argument), self.assertRaisesRegex(ValueError, "four fresh maintained"):
                self.invoke(**{argument: self.f.root / ("build/other-" + argument)})
        self.stage_call.assert_not_called()
        self.assertEqual(self.events, [])
        self.assertFalse(self.journal_path.exists())

    def test_each_preexisting_output_is_preserved_before_any_stage(self):
        for directory in (self.output, self.history, self.appearance, self.drawing):
            directory.mkdir()
            marker = directory / "existing.txt"
            marker.write_bytes(b"preserve previous owned output\n")
            try:
                with self.subTest(directory=directory), self.assertRaisesRegex(ValueError, "four fresh maintained"):
                    self.invoke()
                self.assertEqual(marker.read_bytes(), b"preserve previous owned output\n")
            finally:
                marker.unlink()  # Exact tiny fixture created by this method.
                directory.rmdir()
        self.stage_call.assert_not_called()
        self.assertEqual(self.events, [])
        self.assertFalse(self.journal_path.exists())

    def test_missing_ci_or_stage_profile_refuses_before_any_packet(self):
        for environment in ({}, {"GITHUB_ACTIONS": "true", "GALLIUM_DRIVER": "llvmpipe"}):
            with self.subTest(environment=environment), patch.object(suite.sys, "platform", "win32"), \
                    patch.dict(os.environ, environment, clear=True), \
                    patch.object(suite, "windows_gl_stage", self.real_stage):
                with self.assertRaisesRegex(ValueError, "disposable CI wrapper|stage identity"):
                    self.invoke()
        self.assertEqual(self.events, [])
        self.assertFalse(self.journal_path.exists())
        self.assertTrue(all(not directory.exists() for directory in (self.output, self.history, self.appearance, self.drawing)))

    def test_changed_original_raw_blocks_runtime_callback_and_full(self):
        self.change_raw = True
        with self.assertRaisesRegex(RuntimeError, "Pinned identity differs"):
            self.invoke()
        self.assertEqual(self.events, ["history", "appearance", "drawing"])
        row = self.journal()
        self.assertEqual((row["state"], row["stages"][-1]["state"]), ("failed", "not_run"))
        self.assertFalse(self.output.exists() or (self.provider_output / "runtime-proof.json").exists())
        self.assertEqual((self.appearance / "capture/0.json").read_bytes(), b"changed after the pinned packet\n")


class HistorySuiteSourceLoading(unittest.TestCase):
    def source_loader_ignores_valid_stale_bytecode(self, loader_name, filename):
        # Only the script location is redirected. The actual loader function,
        # import machinery and timestamp-based cache validation remain real.
        with tempfile.TemporaryDirectory(prefix="imagejxx-history-suite-cache-") as temporary, \
                patch.object(sys, "dont_write_bytecode", True), \
                patch.object(sys, "pycache_prefix", None):
            root = Path(temporary).resolve(strict=True)
            scripts = root / "scripts"
            scripts.mkdir()
            source = scripts / filename
            stale = b"SOURCE_MARKER = 'stale'\n"
            fresh = b"SOURCE_MARKER = 'fresh'\n"
            self.assertEqual(len(stale), len(fresh))
            timestamp = 1_700_000_000
            source.write_bytes(stale)
            os.utime(source, ns=(timestamp * 1_000_000_000,) * 2)
            cache = Path(importlib.util.cache_from_source(str(source)))
            py_compile.compile(str(source), cfile=str(cache), doraise=True,
                               invalidation_mode=py_compile.PycInvalidationMode.TIMESTAMP)
            cached_bytes = cache.read_bytes()
            self.assertEqual(cached_bytes[:4], importlib.util.MAGIC_NUMBER)
            self.assertEqual(int.from_bytes(cached_bytes[4:8], "little"), 0)
            self.assertEqual(int.from_bytes(cached_bytes[8:12], "little"), timestamp)
            self.assertEqual(int.from_bytes(cached_bytes[12:16], "little"), len(stale))
            source.write_bytes(fresh)
            os.utime(source, ns=(timestamp * 1_000_000_000,) * 2)
            self.assertEqual(int(source.stat().st_mtime), timestamp)
            self.assertEqual(source.stat().st_size, len(stale))
            source_pin = pin(source)

            spec = importlib.util.spec_from_file_location("suite_stale_cache_control", source)
            legacy = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(legacy)
            self.assertEqual(legacy.SOURCE_MARKER, "stale",
                             "Fixture must expose the old timestamp-valid cache behavior")
            self.assertEqual(pin(source), source_pin)

            with patch.object(suite, "__file__", str(scripts / "ci_python_suite.py")):
                current = getattr(suite, loader_name)()
            self.assertEqual(current.SOURCE_MARKER, "fresh")
            self.assertEqual(Path(current.__file__).resolve(strict=True), source)
            self.assertEqual(pin(source), source_pin)
            self.assertEqual(source.read_bytes(), fresh)
            self.assertEqual(cache.read_bytes(), cached_bytes,
                             "Loading source must leave the causal stale-cache fixture intact")

    def test_history_helper_executes_source_despite_timestamp_valid_stale_bytecode(self):
        self.source_loader_ignores_valid_stale_bytecode(
            "history_helper", "capture_shortcuts_history_ui.py")

    def test_history_builder_executes_source_despite_timestamp_valid_stale_bytecode(self):
        self.source_loader_ignores_valid_stale_bytecode("history_builder", "ci_history_build.py")


if __name__ == "__main__":
    unittest.main()
