"""History-build transport and tiny Windows launch controls; no product builds.

Only tracked-file discovery, the history role provider, and the process owner are
stubbed. Source bytes, cache files, products, receipts and their hashes are real
small temporary files. A successful fixture is not executed ImageJXX evidence.
The separate Windows launch case runs only an isolated standard-library child.
"""
from copy import deepcopy
import base64
import csv
import hashlib
import importlib.metadata
import importlib.util
import json
import os
from pathlib import Path
import py_compile
import subprocess
import sys
import sysconfig
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "ci_history_build_under_test", ROOT / "scripts/ci_history_build.py")
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)

# Independently named original capture inputs, not copied from the selector
# under test at runtime. The history roles override COMMON's old owner role.
HISTORY_PATHS = (
    "tests/python/actual_observation_transport.py",
    "src/desktop/actual_observation_transport.hpp",
    "src/desktop/window.cpp",
    "src/desktop/headless_session_internal.hpp",
    "src/desktop/settings_panel.cpp",
    "src/desktop/settings_panel.hpp",
    "src/native/settings.cpp",
    "src/native/preferences.cpp",
    "src/desktop/python.cpp",
    "src/imagejxx/ui/app.py",
    "docs/parity/commands.json",
    "scripts/capture_shortcuts_history_ui.py",
    "scripts/ci_owned_process.py",
    "src/imagejxx/__init__.py",
    "src/imagejxx/ui/persistence.py",
    "src/desktop/history_settings_ui_probe.cpp",
    "src/desktop/history_settings_ui_probe.hpp",
    "scripts/ci_mac_process.py",
    "src/native/document.cpp",
    "include/imagejxx/document.hpp",
    "src/native/application.cpp",
    "include/imagejxx/application.hpp",
    "src/native/session.cpp",
    "include/imagejxx/session.hpp",
    "tests/python/test_history_settings_ui.py",
    "docs/parity/reviews/G010-history-settings-ownership-001.json",
)
MANDATORY_PATHS = (
    "CMakeLists.txt", "pyproject.toml", "README.md", "LICENSE", "THIRD_PARTY_NOTICES.md",
    ".gitattributes", "scripts/ci_history_build.py", "scripts/ci_python_suite.py",
    ".github/workflows/test.yml", ".github/workflows/source-platform.yml",
    "src/desktop/command_metadata.hpp",
)
EXTRA_SOURCES = (
    "src/native/fixture.cpp", "include/imagejxx/fixture.hpp",
    "third_party/fixture/header.hpp", "cmake/Fixture.cmake",
    "scripts/fixture.py", "licenses/fixture.txt",
    "examples/cpp/header.hpp", "examples/cpp/entry.cpp",
    "tests/native/fixture.cpp", "conftest.py",
)
EXCLUDED_PATHS = (
    ".tmp/fixture.log", ".cache/fixture.dat", "build/fixture.bin",
    "dist/fixture.whl", ".venv/fixture.py", "docs/notes.md",
    "docs/packets/CG0-analytic-reference-contract.md",
    "docs/packets/CG0-imagej-observer-contract.md",
    "tests/reference/continuous_bezier_exact_reference.py",
    "tests/reference/ObserveContinuousPathBooleans.java",
)


def pin(path):
    path = Path(path).resolve(strict=True)
    raw = path.read_bytes()
    return {"path": str(path), "bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest()}


class OwnerFailure(RuntimeError):
    def __init__(self, receipt):
        super().__init__("Synthetic owner failed after writing its terminal receipt")
        self.receipt = receipt


class HistoryBuildTransport(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx-history-build-metadata-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve(strict=True)
        sysconfig.get_config_var("EXT_SUFFIX")  # Prime native config before the synthetic platform mock.
        # Darwin build ownership is intentionally unqualified. These are
        # metadata-only Linux fixtures there, never native process evidence;
        # the dedicated refusal control below still selects actual Darwin.
        if sys.platform == "darwin":
            self.start_patch(patch.object(build.sys, "platform", "linux"))
        self.output = self.root / "build/ci-history-build"
        self.cache = self.root / "build/wheel/synthetic"
        self.site = self.root / "installation/site-packages"
        self.dist_info = self.site / "imagejxx-0.2.0.dist-info"
        self.distribution = importlib.metadata.PathDistribution(self.dist_info)
        self.argv = [str(Path(sys.executable).resolve(strict=True)), "-m", "pip", "install",
                     ".[io,test]", "-C", "cmake.build-type=Release", "-Ccmake.define.BUILD_TESTING=ON"]
        self.tracked = list(MANDATORY_PATHS + EXTRA_SOURCES + EXCLUDED_PATHS)
        for logical in set(self.tracked) | set(HISTORY_PATHS):
            self.write(logical, ("SYNTHETIC, NEVER EXECUTED: " + logical + "\n").encode())
        self.helper_pin = pin(self.root / "scripts/ci_owned_process.py")
        self.calls = []
        self.freeze_bytes_at_command = None
        self.after_products = None
        self.after_install = None
        self.installed_paths = {}
        self.installed_rows = []
        self.proof_changes = {}
        self.durable_changes = {}
        self.owner_exception = False
        self.emit_products = True
        self.cache_changes = {}
        self.output_number = 0
        self.start_patch(patch.object(build, "_tracked_paths", side_effect=lambda root: list(self.tracked)))
        self.start_patch(patch.object(build, "_history_paths", return_value=set(HISTORY_PATHS)))
        self.owner_loader = self.start_patch(patch.object(
            build, "_load_owner", return_value=(self.execute, self.helper_pin)))
        self.distribution_loader = self.start_patch(patch.object(
            importlib.metadata, "distribution", side_effect=self.installed_distribution))
        self.start_patch(patch.object(importlib, "import_module", side_effect=AssertionError(
            "The build wrapper may read distribution metadata, never import imagejxx or NumPy")))
        self.start_patch(patch.object(subprocess, "Popen", side_effect=AssertionError(
            "Synthetic controls must not execute pip, Git, a compiler, or a product")))
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
        return self.write(relative, (json.dumps(value, sort_keys=True) + "\n").encode())

    def read_json(self, path):
        return json.loads(Path(path).read_bytes())

    def cache_fields(self, directory=None):
        return {
            "CMAKE_GENERATOR": "Ninja", "CMAKE_BUILD_TYPE": "Release",
            "CMAKE_CONFIGURATION_TYPES": "", "BUILD_TESTING": "ON",
            "IMAGEJXX_BUILD_PYTHON": "ON", "IMAGEJXX_BUILD_IO": "ON",
            "IMAGEJXX_BUILD_CPP_UI": "ON", "IMAGEJXX_BUILD_PYTHON_UI": "ON",
            "CMAKE_HOME_DIRECTORY": str(self.root),
            "CMAKE_CACHEFILE_DIR": str(directory or self.cache),
        }

    def materialize_cache(self, directory=None):
        directory = directory or self.cache
        fields = self.cache_fields(directory)
        fields.update(self.cache_changes)
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "CMakeCache.txt").write_text(
            "# Synthetic configuration: no CMake execution\n" +
            "".join(key + ":STRING=" + value + "\n" for key, value in fields.items()),
            encoding="utf-8")
        suffix = sysconfig.get_config_var("EXT_SUFFIX")
        self.assertIsInstance(suffix, str)
        names = {"cpp": "imagejxx.exe" if sys.platform == "win32" else "imagejxx",
                 "_native": "_native" + suffix, "_desktop": "_desktop" + suffix}
        paths = {}
        for role, name in names.items():
            paths[role] = directory / name
            paths[role].write_bytes(("synthetic compiled " + role + "; never executed\n").encode())
        return paths

    def installed_distribution(self, name):
        self.assertEqual(name, "imagejxx", "Only the actual imagejxx distribution is relevant")
        return self.distribution

    @staticmethod
    def record_row(logical, path):
        raw = path.read_bytes()
        digest = base64.urlsafe_b64encode(hashlib.sha256(raw).digest()).rstrip(b"=").decode("ascii")
        return [logical, "sha256=" + digest, str(len(raw))]

    def write_record(self):
        path = self.dist_info / "RECORD"
        with path.open("w", encoding="utf-8", newline="") as stream:
            csv.writer(stream, lineterminator="\n").writerows(self.installed_rows)
        return path

    def materialize_install(self):
        """Real tiny distribution metadata, not a fabricated validator result.

        The final payloads deliberately differ from the raw cache payloads.
        This is a synthetic packaging-phase association, never strip proof.
        """
        self.dist_info.mkdir(parents=True, exist_ok=True)
        metadata = self.dist_info / "METADATA"
        metadata.write_bytes(b"Metadata-Version: 2.1\nName: imagejxx\nVersion: 0.2.0\n\n")
        direct_url = self.dist_info / "direct_url.json"
        direct_url.write_text(json.dumps({"url": self.root.as_uri(), "dir_info": {}}) + "\n",
                              encoding="utf-8")
        suffix = sysconfig.get_config_var("EXT_SUFFIX")
        logical = {"cpp": "../bin/" + ("imagejxx.exe" if sys.platform == "win32" else "imagejxx"),
                   "_native": "imagejxx/_native" + suffix,
                   "_desktop": "imagejxx/_desktop" + suffix}
        self.installed_paths = {}
        self.installed_rows = []
        for role, name in logical.items():
            path = (self.site / name).resolve()
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(("synthetic final installed " + role + "; differs from raw; never executed\n").encode())
            self.installed_paths[role] = path
            self.installed_rows.append(self.record_row(name, path))
        for path in (metadata, direct_url):
            self.installed_rows.append(self.record_row(path.relative_to(self.site).as_posix(), path))
        self.installed_rows.append([self.dist_info.name + "/RECORD", "", ""])
        self.write_record()
        return self.installed_paths

    def replace_recorded_metadata(self, name, raw):
        path = self.dist_info / name
        path.write_bytes(raw)
        logical = path.relative_to(self.site).as_posix()
        index = next(index for index, row in enumerate(self.installed_rows) if row[0] == logical)
        self.installed_rows[index] = self.record_row(logical, path)
        self.write_record()

    def change_selected_record(self, role, column, value):
        path = self.installed_paths[role]
        row = next(row for row in self.installed_rows if (self.site / row[0]).resolve() == path)
        row[column] = value
        self.write_record()

    def execute(self, argv, **options):
        # The command must observe an already durable freeze. Product creation
        # happens only afterwards, so this detects a post-build repin shortcut.
        self.calls.append((deepcopy(argv), options))
        freeze = self.output / "source-freeze.json"
        self.assertTrue(freeze.is_file())
        self.freeze_bytes_at_command = freeze.read_bytes()
        document = self.read_json(freeze)
        self.assertEqual(document["schema_version"], 1)
        self.assertEqual(Path(document["repository"]), self.root)
        frozen = {row["logical_path"]: row for row in document["sources"]}
        self.assertTrue(set(HISTORY_PATHS) <= set(frozen))
        for logical in HISTORY_PATHS:
            self.assertEqual({key: frozen[logical][key] for key in ("path", "bytes", "sha256")},
                             pin(self.root / logical))
        paths = self.materialize_cache() if self.emit_products else {}
        if self.after_products:
            self.after_products(paths)
        self.materialize_install()
        if self.after_install:
            self.after_install(self.installed_paths)
        options["stdout"].write(b"synthetic owned install output; not an actual build\n")
        options["stdout"].flush()
        proof = {"schema": "imagejxx.owned-command.001", "state": "exited",
                 "exit_code": 0, "target_completed": True, "cleanup_complete": True,
                 "timed_out": False, "argv": list(argv), "cwd": str(self.root),
                 "helper": deepcopy(self.helper_pin)}
        proof.update(deepcopy(self.proof_changes))
        durable = deepcopy(proof)
        durable.update(deepcopy(self.durable_changes))
        Path(options["receipt_path"]).write_text(json.dumps(durable) + "\n", encoding="utf-8")
        if self.owner_exception:
            raise OwnerFailure(proof)
        return proof

    def run_build(self, argv=None):
        self.output_number += 1
        if self.output_number > 1:
            self.output = self.root / ("build/ci-history-build-" + str(self.output_number))
        return build.run_build(self.root, self.output, self.argv if argv is None else argv, timeout=17)

    def failed(self, argv=None):
        with self.assertRaises((ValueError, RuntimeError, OSError)):
            self.run_build(argv)
        receipt = self.read_json(self.output / "receipt.json")
        self.assertEqual(receipt["state"], "failed")
        self.assertIs(receipt["acceptance"], False)
        self.assertTrue(receipt["error"])
        return receipt

    def make_freeze(self):
        self.output.mkdir(parents=True)
        result = build.freeze_sources(self.root, self.output)
        self.assertEqual(result, self.output / "source-freeze.json")
        return result

    def symlink(self, link, target, *, directory=False):
        try:
            link.symlink_to(target, target_is_directory=directory)
        except (OSError, NotImplementedError) as error:
            self.skipTest("Host cannot create this owned link fixture: " + str(error))

    def test_freeze_covers_literal_history_and_tracked_closure_without_generated_junk(self):
        path = self.make_freeze()
        value = build.verify_sources(path)
        names = [row["logical_path"] for row in value["sources"]]
        expected = set(HISTORY_PATHS + MANDATORY_PATHS + EXTRA_SOURCES)
        self.assertEqual(set(names), expected)
        self.assertEqual(len(names), len(expected))
        self.assertFalse(set(EXCLUDED_PATHS) & set(names))
        self.assertEqual(value, self.read_json(path))
        for row in value["sources"]:
            self.assertEqual({key: row[key] for key in ("path", "bytes", "sha256")},
                             pin(self.root / row["logical_path"]))

    def test_history_roles_are_included_even_when_untracked(self):
        self.tracked = []
        names = {row["logical_path"] for row in self.read_json(self.make_freeze())["sources"]}
        self.assertTrue(set(HISTORY_PATHS + MANDATORY_PATHS) <= names)

    def test_freeze_output_is_exclusive(self):
        path = self.make_freeze()
        original = path.read_bytes()
        with self.assertRaises((ValueError, RuntimeError, OSError)):
            build.freeze_sources(self.root, self.output)
        self.assertEqual(path.read_bytes(), original)

    def test_missing_original_history_input_refuses_before_freeze(self):
        (self.root / HISTORY_PATHS[-1]).unlink()
        self.output.mkdir(parents=True)
        with self.assertRaises((ValueError, RuntimeError, OSError)):
            build.freeze_sources(self.root, self.output)
        self.assertFalse((self.output / "source-freeze.json").exists())

    def test_verify_refuses_same_size_mutation_and_keeps_original_freeze(self):
        path = self.make_freeze()
        frozen = path.read_bytes()
        source = self.root / "src/desktop/window.cpp"
        original = source.read_bytes()
        source.write_bytes(b"X" + original[1:])
        with self.assertRaises((ValueError, RuntimeError, OSError)):
            build.verify_sources(path)
        self.assertEqual(path.read_bytes(), frozen)

    def test_source_leaf_link_is_refused(self):
        source = self.root / "src/desktop/window.cpp"
        source.unlink()
        self.symlink(source, self.root / "src/native/settings.cpp")
        self.output.mkdir(parents=True)
        with self.assertRaises((ValueError, RuntimeError, OSError)):
            build.freeze_sources(self.root, self.output)

    def test_passed_transport_preserves_argv_freeze_and_exact_three_products(self):
        result = self.run_build()
        self.assertEqual(result, self.read_json(self.output / "receipt.json"))
        self.assertEqual(result["state"], "passed")
        self.assertEqual(result["kind"], "imagejxx.hosted-history-build.001")
        self.assertEqual(result["schema_version"], 1)
        self.assertIs(result["acceptance"], False)
        self.assertEqual(len(self.calls), 1)
        argv, options = self.calls[0]
        self.assertEqual(argv, self.argv)
        self.assertEqual(result["argv"], self.argv)
        self.assertEqual(Path(options["cwd"]), self.root)
        self.assertEqual(options["timeout"], 17)
        self.assertEqual(result["source_freeze"], pin(self.output / "source-freeze.json"))
        self.assertEqual((self.output / "source-freeze.json").read_bytes(), self.freeze_bytes_at_command)
        self.assertEqual(set(result["built_products"]), {"cpp", "_native", "_desktop"})
        self.assertEqual(set(result["raw_cache_products"]), {"cpp", "_native", "_desktop"})
        for role, value in result["raw_cache_products"].items():
            self.assertEqual(Path(value["path"]).parent, self.cache)
            self.assertEqual(value, pin(value["path"]))
            self.assertNotEqual(value["sha256"], result["built_products"][role]["sha256"])
        self.assertEqual(result["built_products"], {role: pin(path) for role, path in self.installed_paths.items()})
        self.assertEqual(result["packaging"]["phase"], "pip-installed-final-outputs")
        self.assertEqual(result["packaging"]["record"], pin(self.dist_info / "RECORD"))
        self.assertEqual(result["packaging"]["metadata"], pin(self.dist_info / "METADATA"))
        self.assertEqual(result["packaging"]["direct_url"], pin(self.dist_info / "direct_url.json"))
        self.assertEqual(result["packaging"]["source_url"], self.root.as_uri())
        self.assertEqual(result["packaging"]["distribution_name"], "imagejxx")
        self.assertEqual(result["packaging"]["distribution_version"], "0.2.0")
        self.assertEqual(set(result["packaging"]["products"]), {"cpp", "_native", "_desktop"})
        for role, proof in result["packaging"]["products"].items():
            row = next(row for row in self.installed_rows
                       if (self.site / row[0]).resolve() == self.installed_paths[role])
            self.assertEqual(proof, {"record_path": row[0], "record_hash": row[1], "record_size": int(row[2])})
        self.distribution_loader.assert_called_once_with("imagejxx")
        self.assertEqual(result["cache"], pin(self.cache / "CMakeCache.txt"))
        self.assertEqual(result["owner_helper"], self.helper_pin)
        self.assertEqual(result["process"], pin(self.output / "process.json"))
        self.assertEqual(result["command_proof"], self.read_json(self.output / "process.json"))
        self.assertEqual(result["log"], pin(result["log"]["path"]))
        self.assertEqual(set(result["source_times"]), {"frozen", "verified_after_command", "verified_final"})
        times = result["source_times"]
        self.assertLessEqual(times["frozen"], times["verified_after_command"])
        self.assertLessEqual(times["verified_after_command"], times["verified_final"])

    def test_source_mutation_during_command_never_repins_or_passes(self):
        self.after_products = lambda paths: (self.root / "src/desktop/window.cpp").write_bytes(b"changed\n")
        result = self.failed()
        self.assertEqual((self.output / "source-freeze.json").read_bytes(), self.freeze_bytes_at_command)
        self.assertEqual(self.read_json(self.output / "process.json")["exit_code"], 0)
        self.assertNotEqual(result["state"], "passed")

    def test_new_tracked_source_during_command_does_not_rewrite_membership(self):
        def add_source(paths):
            self.write("src/native/new.cpp", b"new synthetic source\n")
            self.tracked.append("src/native/new.cpp")
        self.after_products = add_source
        self.failed()
        self.assertEqual((self.output / "source-freeze.json").read_bytes(), self.freeze_bytes_at_command)

    def test_incremental_build_may_legitimately_keep_exact_product_bytes(self):
        before = {name: pin(path) for name, path in self.materialize_cache().items()}
        installed_before = {name: pin(path) for name, path in self.materialize_install().items()}
        result = self.run_build()
        self.assertEqual(result["raw_cache_products"], before)
        self.assertEqual(result["built_products"], installed_before)
        self.assertEqual(len(self.calls), 1, "Matching products still require an actual owned command")

    def test_nonzero_owned_command_is_retained_and_never_passed(self):
        self.proof_changes["exit_code"] = 23
        result = self.failed()
        self.assertEqual(self.read_json(self.output / "process.json")["exit_code"], 23)
        self.assertEqual(result["process"], pin(self.output / "process.json"))

    def test_incomplete_or_timed_out_owner_proof_is_refused(self):
        for changes in ({"cleanup_complete": False}, {"target_completed": False},
                        {"timed_out": True}, {"state": "timed_out"}, {"schema": "unrelated-proof"}):
            with self.subTest(changes=changes):
                self.proof_changes = changes
                self.failed()

    def test_durable_and_returned_owner_rows_must_be_identical(self):
        self.durable_changes["exit_code"] = 4
        self.failed()

    def test_owner_argv_cwd_and_helper_are_bound(self):
        for changes in ({"argv": self.argv + ["--unexpected"]},
                        {"cwd": str(self.root.parent)},
                        {"helper": {**self.helper_pin, "sha256": "0" * 64}}):
            with self.subTest(changes=changes):
                self.proof_changes = changes
                self.failed()

    def test_owner_exception_retains_failed_receipt_and_log(self):
        self.owner_exception = True
        self.proof_changes.update(state="failed", exit_code=None, target_completed=False)
        result = self.failed()
        self.assertEqual(result["process"], pin(self.output / "process.json"))
        self.assertTrue(Path(result["log"]["path"]).is_file())

    def test_preexisting_output_refuses_without_owner_or_rewrite(self):
        self.output.mkdir(parents=True)
        marker = self.output / "unrelated.txt"
        marker.write_bytes(b"preserve existing caller data\n")
        with self.assertRaises((ValueError, RuntimeError, OSError)):
            self.run_build()
        self.assertEqual(marker.read_bytes(), b"preserve existing caller data\n")
        self.assertEqual(self.calls, [])
        self.assertFalse((self.output / "receipt.json").exists())

    def test_darwin_refuses_before_creating_output_or_loading_owner(self):
        with patch.object(build.sys, "platform", "darwin"):
            with self.assertRaises((ValueError, RuntimeError, NotImplementedError)):
                self.run_build()
        self.assertFalse(self.output.exists())
        self.assertEqual(self.calls, [])
        self.owner_loader.assert_not_called()

    def test_unrelated_install_or_command_never_launches_owner(self):
        variants = (
            self.argv[:4] + ["unrelated-package"],
            [self.argv[0], "-m", "pip", "download", ".[io,test]"],
            [self.argv[0], "-c", "raise SystemExit(0)"],
        )
        for index, argv in enumerate(variants):
            with self.subTest(argv=argv):
                self.output = self.root / ("build/bad-command-" + str(index))
                with self.assertRaises((ValueError, RuntimeError, OSError)):
                    build.run_build(self.root, self.output, argv, timeout=17)
        self.assertEqual(self.calls, [])

    def test_dry_run_or_help_cannot_authenticate_preexisting_cache_products(self):
        products = {name: pin(path) for name, path in self.materialize_cache().items()}
        for index, tail in enumerate(("--dry-run", "--help", "--version", "--no-deps", "other-package")):
            with self.subTest(tail=tail):
                output = self.root / ("build/forbidden-tail-" + str(index))
                with self.assertRaises((ValueError, RuntimeError, OSError)):
                    build.run_build(self.root, output, self.argv + [tail], timeout=17)
                self.assertFalse(output.exists())
        self.assertEqual(self.calls, [])
        for product in products.values():
            self.assertEqual(pin(product["path"]), product)

    def test_configuration_tail_requires_unique_nonempty_key_value_pairs(self):
        tails = (["-C"], ["-C", "no-equals"], ["-C=ON"], ["-Ckey="],
                 ["-C", "cmake.define.BUILD_TESTING=ON", "-Ccmake.define.BUILD_TESTING=OFF"],
                 ["-Cnonascii-λ=ON"])
        for index, tail in enumerate(tails):
            with self.subTest(tail=tail):
                output = self.root / ("build/invalid-config-" + str(index))
                with self.assertRaises((ValueError, RuntimeError, OSError)):
                    build.run_build(self.root, output, self.argv[:5] + tail, timeout=17)
                self.assertFalse(output.exists())
        self.assertEqual(self.calls, [])

    def test_configuration_cannot_disable_build_or_select_unreviewed_target(self):
        tails = ("-Cwheel.cmake=false", "-Cbuild.targets=unrelated",
                 "-Ccmake.args=--help", "-Ccmake.args=-GUnix Makefiles")
        self.materialize_cache()
        for index, tail in enumerate(tails):
            with self.subTest(tail=tail):
                output = self.root / ("build/unreviewed-config-" + str(index))
                with self.assertRaises((ValueError, RuntimeError, OSError)):
                    build.run_build(self.root, output, self.argv + [tail], timeout=17)
                self.assertFalse(output.exists())
        self.assertEqual(self.calls, [])

    def test_success_without_actual_cache_does_not_admit_installed_lookalikes(self):
        self.emit_products = False
        self.write("installed/imagejxx", b"unrelated stale installed application")
        self.failed()

    def test_terminal_log_bound_refuses_without_truncating_original_output(self):
        with patch.object(build, "MAX_LOG_BYTES", 8):
            result = self.failed()
        self.assertNotIn("log", result)
        raw = Path(result["log_retained_unpinned"]["path"]).read_bytes()
        self.assertEqual(raw, b"synthetic owned install output; not an actual build\n")
        self.assertEqual(result["log_retained_unpinned"]["bytes"], len(raw))
        self.assertEqual(self.read_json(self.output / "process.json")["exit_code"], 0)

    def test_empty_ordinary_tracked_source_is_allowed_but_role_is_not(self):
        ordinary = "src/native/empty_translation_unit.cpp"
        self.tracked.append(ordinary)
        self.write(ordinary, b"")
        frozen = self.read_json(self.make_freeze())
        row = next(row for row in frozen["sources"] if row["logical_path"] == ordinary)
        self.assertEqual(row["bytes"], 0)
        self.assertEqual(row["sha256"], hashlib.sha256(b"").hexdigest())
        (self.root / "src/desktop/history_settings_ui_probe.cpp").write_bytes(b"")
        with self.assertRaises((ValueError, RuntimeError, OSError)):
            build.verify_sources(self.output / "source-freeze.json")
        fresh = self.root / "build/empty-role-freeze"
        fresh.mkdir()
        with self.assertRaises((ValueError, RuntimeError, OSError)):
            build.freeze_sources(self.root, fresh)
        self.assertFalse((fresh / "source-freeze.json").exists())

    def test_duplicate_cache_roots_are_refused(self):
        self.after_products = lambda paths: self.materialize_cache(self.root / "build/wheel/second")
        self.failed()

    def test_cache_generator_build_type_and_multiconfig_are_strict(self):
        for changes in ({"CMAKE_GENERATOR": "Unix Makefiles"},
                        {"CMAKE_BUILD_TYPE": "Debug"},
                        {"CMAKE_CONFIGURATION_TYPES": "Debug;Release"}):
            with self.subTest(changes=changes):
                self.cache_changes = changes
                self.failed()

    def test_each_required_build_feature_must_be_enabled(self):
        for key in ("BUILD_TESTING", "IMAGEJXX_BUILD_PYTHON", "IMAGEJXX_BUILD_IO",
                    "IMAGEJXX_BUILD_CPP_UI", "IMAGEJXX_BUILD_PYTHON_UI"):
            with self.subTest(key=key):
                self.cache_changes = {key: "OFF"}
                self.failed()

    def test_cache_cannot_belong_to_another_source_or_cache_directory(self):
        for changes in ({"CMAKE_HOME_DIRECTORY": str(self.root.parent)},
                        {"CMAKE_CACHEFILE_DIR": str(self.root / "other-cache")}):
            with self.subTest(changes=changes):
                self.cache_changes = changes
                self.failed()

    def test_all_three_compiled_products_are_required(self):
        for role in ("cpp", "_native", "_desktop"):
            with self.subTest(role=role):
                self.after_products = lambda paths, role=role: paths[role].unlink()
                self.failed()

    def test_empty_product_is_not_compiled_identity(self):
        self.after_products = lambda paths: paths["_desktop"].write_bytes(b"")
        self.failed()

    def test_release_subdirectory_is_not_the_declared_single_config_product(self):
        def move_products(paths):
            release = self.cache / "Release"
            release.mkdir()
            for path in paths.values():
                path.rename(release / path.name)
        self.after_products = move_products
        self.failed()

    def test_linked_product_is_refused_even_when_target_bytes_match(self):
        target = self.write("separate-product.bin", b"synthetic compiled _desktop; never executed\n")
        def link_product(paths):
            path = paths["_desktop"]
            path.unlink()
            self.symlink(path, target)
        self.after_products = link_product
        self.failed()

    def test_linked_cache_ancestor_is_refused(self):
        real = self.root / "real-cache"
        self.materialize_cache(real)
        wheel = self.root / "build/wheel"
        wheel.mkdir(parents=True)
        self.symlink(self.cache, real, directory=True)
        self.emit_products = False
        self.failed()

    def test_changed_installed_payload_refuses_original_record_hash(self):
        self.after_install = lambda paths: paths["_native"].write_bytes(b"counterfeit installed bytes\n")
        self.failed()
        self.assertEqual(self.read_json(self.output / "process.json")["exit_code"], 0)

    def test_selected_record_requires_exact_sha256_and_original_size(self):
        zero_hash = "sha256=" + base64.urlsafe_b64encode(bytes(32)).rstrip(b"=").decode("ascii")
        for column, value in ((1, zero_hash), (1, ""), (1, "md5=" + "a" * 22),
                              (2, "1"), (2, "0001"), (2, ""), (2, "not-a-size")):
            with self.subTest(column=column, value=value):
                self.after_install = lambda paths, column=column, value=value: self.change_selected_record(
                    "_desktop", column, value)
                self.failed()

    def test_duplicate_selected_record_row_refuses(self):
        def duplicate(paths):
            self.installed_rows.insert(1, list(self.installed_rows[0]))
            self.write_record()
        self.after_install = duplicate
        self.failed()

    def test_missing_selected_record_row_refuses_even_when_file_exists(self):
        def omit(paths):
            self.installed_rows = [row for row in self.installed_rows
                                   if (self.site / row[0]).resolve() != paths["_native"]]
            self.write_record()
        self.after_install = omit
        self.failed()
        self.assertTrue(self.installed_paths["_native"].is_file())

    def test_application_record_basename_must_be_unique(self):
        def duplicate_application(paths):
            other = self.site / "other" / paths["cpp"].name
            other.parent.mkdir()
            other.write_bytes(paths["cpp"].read_bytes())
            self.installed_rows.insert(0, self.record_row(other.relative_to(self.site).as_posix(), other))
            self.write_record()
        self.after_install = duplicate_application
        self.failed()

    def test_native_record_must_be_in_imagejxx_package(self):
        def move_record(paths):
            original = paths["_native"]
            other = self.site / "foreign" / original.name
            other.parent.mkdir()
            other.write_bytes(original.read_bytes())
            index = next(index for index, row in enumerate(self.installed_rows)
                         if (self.site / row[0]).resolve() == original)
            self.installed_rows[index] = self.record_row(other.relative_to(self.site).as_posix(), other)
            self.write_record()
        self.after_install = move_record
        self.failed()

    def test_direct_url_must_identify_this_exact_local_source(self):
        for value in ({"url": self.root.parent.as_uri(), "dir_info": {}},
                      {"url": self.root.as_uri() + "?different=source", "dir_info": {}},
                      {"url": self.root.as_uri() + "#different", "dir_info": {}},
                      {"url": "https://example.invalid/imagejxx", "dir_info": {}},
                      {"url": self.root.as_uri(), "dir_info": {"editable": True}},
                      {"url": self.root.as_uri(), "archive_info": {}}):
            with self.subTest(value=value):
                self.after_install = lambda paths, value=value: self.replace_recorded_metadata(
                    "direct_url.json", (json.dumps(value) + "\n").encode())
                self.failed()

    def test_explicit_noneditable_local_source_remains_valid(self):
        self.after_install = lambda paths: self.replace_recorded_metadata(
            "direct_url.json", (json.dumps({"url": self.root.as_uri(),
                                            "dir_info": {"editable": False}}) + "\n").encode())
        result = self.run_build()
        self.assertEqual(result["packaging"]["source_url"], self.root.as_uri())

    def test_metadata_and_direct_url_bytes_are_bound_by_original_record(self):
        for name in ("METADATA", "direct_url.json"):
            with self.subTest(name=name):
                def alter_without_resigning(paths, name=name):
                    path = self.dist_info / name
                    path.write_bytes(path.read_bytes() + b"\n")
                self.after_install = alter_without_resigning
                self.failed()

    def test_wrong_distribution_name_refuses_even_when_metadata_record_matches(self):
        self.after_install = lambda paths: self.replace_recorded_metadata(
            "METADATA", b"Metadata-Version: 2.1\nName: different-package\nVersion: 0.2.0\n\n")
        self.failed()

    def test_missing_record_cannot_be_replaced_with_cache_or_metadata_claims(self):
        self.after_install = lambda paths: (self.dist_info / "RECORD").unlink()
        self.failed()

    def test_record_self_row_requires_original_empty_hash_and_size(self):
        def change_self(paths):
            self.installed_rows[-1] = [self.dist_info.name + "/RECORD", "sha256=claimed", "123"]
            self.write_record()
        self.after_install = change_self
        self.failed()

    def test_final_product_cannot_be_the_raw_cache_path(self):
        def use_raw_app(paths):
            original = paths["cpp"]
            raw = self.cache / original.name
            relative = Path(os.path.relpath(raw, self.site)).as_posix()
            index = next(index for index, row in enumerate(self.installed_rows)
                         if (self.site / row[0]).resolve() == original)
            self.installed_rows[index] = self.record_row(relative, raw)
            self.write_record()
        self.after_install = use_raw_app
        self.failed()

    def test_linked_final_product_is_refused_despite_matching_record_bytes(self):
        def replace_with_link(paths):
            product = paths["_desktop"]
            target = self.write("outside-final-module.bin", product.read_bytes())
            product.unlink()
            self.symlink(product, target)
        self.after_install = replace_with_link
        self.failed()

    def test_bare_python_alias_normalizes_the_actual_owned_argv_and_pin(self):
        executable = str(Path(sys.executable).resolve(strict=True))
        requested = ["python"] + self.argv[1:] + ["-C", "cmake.define.LITERAL_VALUE=space λ"]
        original = list(requested)
        with patch.object(build.shutil, "which", return_value=executable) as resolve:
            normalized, executable_pin = build._command(requested)
            self.assertEqual(normalized, [executable] + original[1:])
            self.assertEqual(executable_pin, pin(executable))
            self.assertEqual(requested, original)
            self.assertIsNot(normalized, requested)
            result = self.run_build(requested)
        self.assertEqual(resolve.call_args_list, [unittest.mock.call("python")] * 2)
        self.assertEqual(requested, original)
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(self.calls[0][0], normalized)
        self.assertEqual(result["argv"], normalized)
        self.assertEqual(result["command_proof"]["argv"], normalized)
        self.assertEqual(result["launch_executable"], executable_pin)
        self.assertEqual(self.read_json(self.output / "process.json")["argv"], normalized)

    def test_missing_or_foreign_python_alias_refuses_before_owner_and_output(self):
        foreign = self.write("foreign-python.exe", b"Synthetic foreign interpreter; never executed\n")
        requested = ["python"] + self.argv[1:]
        for selected in (None, str(foreign)):
            with self.subTest(selected=selected), patch.object(build.shutil, "which", return_value=selected):
                with self.assertRaisesRegex(ValueError, "actual Python interpreter"):
                    build.run_build(self.root, self.output, requested, timeout=17)
                self.assertFalse(self.output.exists())
        self.assertEqual(self.calls, [])
        self.owner_loader.assert_not_called()


@unittest.skipUnless(sys.platform == "win32", "Actual Windows native owner launch control; not a portable simulation")
class HistoryBuildWindowsLaunch(unittest.TestCase):
    def test_normalized_python_launches_harmless_child_after_bare_alias_refusal(self):
        # Keep the exact small inputs/receipts for the root verifier, including
        # failures. No pip, product, detached child, or test-authored PID signal.
        parent = ROOT / ".tmp"
        parent.mkdir(exist_ok=True)
        info = parent.lstat()
        self.assertFalse(parent.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400)
        root = Path(tempfile.mkdtemp(prefix="history-python-launch-", dir=parent)).resolve(strict=True)
        self.assertEqual(root.parent, parent.resolve(strict=True))
        cwd = root / "empty-launch-directory"
        cwd.mkdir()
        child = root / "child.py"
        child.write_text("import json,os,sys\nfrom pathlib import Path\n"
                         "Path(sys.argv[1]).write_text(json.dumps({'pid':os.getpid(),'executable':sys.executable,'value':'normalized-python'}))\n"
                         "print('owned-normalized-python')\n", encoding="utf-8")
        executable = str(Path(sys.executable).resolve(strict=True))
        requested = ["python", "-m", "pip", "install", ".[io,test]", "-Ccmake.build-type=Release"]
        with patch.object(build.shutil, "which", return_value=executable):
            normalized, executable_pin = build._command(requested)
        self.assertEqual(normalized, [executable] + requested[1:])
        self.assertEqual(executable_pin, pin(executable))
        owned = build._load_script(ROOT, "ci_owned_process")
        environment = os.environ.copy()
        environment["PATH"] = str(cwd)
        rows = {}
        print("HISTORY_PYTHON_LAUNCH_SCRATCH=" + str(root), flush=True)
        try:
            bad_argv = [requested[0], "-I", "-B", str(child), str(root / "bare-child.json")]
            with (root / "bare.log").open("xb") as log, self.assertRaises(owned.OwnedProcessError) as failed:
                owned.run_owned(bad_argv, cwd=cwd, env=environment, timeout=10,
                    stdout=log, stderr=subprocess.STDOUT, receipt_path=root / "bare-process.json")
            bad = failed.exception.receipt
            rows["bare"] = bad
            self.assertEqual(json.loads((root / "bare-process.json").read_bytes()), bad)
            self.assertEqual(bad["argv"], bad_argv)
            self.assertEqual(bad["state"], "launch_failed")
            self.assertIsNone(bad["target_pid"])
            self.assertIsNone(bad["exit_code"])
            self.assertIs(bad["target_completed"], False)
            self.assertIs(bad["cleanup_complete"], True)
            self.assertEqual(bad["cleanup"]["remaining"], [])
            self.assertFalse((root / "bare-child.json").exists())
            good_argv = [normalized[0], "-I", "-B", str(child), str(root / "child.json")]
            with (root / "child.log").open("xb") as log:
                good = owned.run_owned(good_argv, cwd=cwd, env=environment, timeout=10,
                    stdout=log, stderr=subprocess.STDOUT, receipt_path=root / "process.json")
            rows["normalized"] = good
            self.assertEqual(json.loads((root / "process.json").read_bytes()), good)
            self.assertEqual(good["argv"], good_argv)
            self.assertEqual((good["state"], good["exit_code"]), ("exited", 0))
            self.assertIs(good["target_completed"], True)
            self.assertIs(good["cleanup_complete"], True)
            self.assertIs(good["timed_out"], False)
            self.assertEqual(good["cleanup"]["remaining"], [])
            self.assertEqual(good["ownership"]["method"], "windows-job")
            self.assertIs(good["ownership"]["assigned_before_resume"], True)
            self.assertIs(good["ownership"]["handles_closed"], True)
            self.assertEqual(good["cleanup"]["terminal_wait"], "job_empty_and_target_signalled")
            observed = json.loads((root / "child.json").read_bytes())
            self.assertEqual(observed["value"], "normalized-python")
            self.assertTrue(Path(observed["executable"]).samefile(executable))
            self.assertEqual((root / "child.log").read_text().strip(), "owned-normalized-python")
            self.assertEqual(pin(executable), executable_pin)
        finally:
            # A Windows venv launcher and its interpreter can have different
            # PIDs. The Job's terminal drain, not PID equality, proves cleanup.
            evidence = {"scope": "actual Windows tiny Python launch only; no build", "acceptance_claim": False,
                        "scratch_retained": str(root), "requested_build_argv": requested,
                        "normalized_build_argv": normalized, "launch_executable": executable_pin,
                        "owner_helper": pin(ROOT / "scripts/ci_owned_process.py"), "rows": rows}
            for name in ("bare-process.json", "process.json"):
                path = root / name
                if path.is_file():
                    evidence.setdefault("durable_receipts", {})[name] = json.loads(path.read_bytes())
            (root / "result.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
            print("HISTORY_PYTHON_LAUNCH_RESULT=" + json.dumps(evidence, sort_keys=True), flush=True)


class HistoryBuildRoleSelection(unittest.TestCase):
    def test_drawing_reader_oracles_and_native_producer_enter_the_build_freeze(self):
        selected = build._history_paths(ROOT)
        self.assertTrue({"src/desktop/settings_drawing_ui_probe.cpp",
                         "src/desktop/settings_drawing_ui_probe.hpp",
                         "tests/python/test_settings_drawing_ui.py",
                         "tests/python/settings_drawing_reader.py",
                         "tests/python/settings_drawing_expectations.py",
                         "tests/native/rounded_path_reference.inc"} <= selected)

    def test_actual_role_selector_preserves_both_packets_with_repeated_role_names(self):
        # Exercise the real source-loaded selector; transport fixtures elsewhere
        # intentionally replace this seam and cannot detect dict-key collisions.
        selected = build._history_paths(ROOT)
        self.assertTrue({"tests/python/test_history_settings_ui.py",
                         "src/desktop/history_settings_ui_probe.cpp",
                         "src/desktop/history_settings_ui_probe.hpp",
                         "tests/python/test_settings_appearance_ui.py",
                         "tests/python/settings_appearance_reader.py",
                         "tests/python/settings_appearance_expectations.py",
                         "src/desktop/settings_appearance_ui_probe.cpp",
                         "src/desktop/settings_appearance_ui_probe.hpp",
                         "scripts/ci_owned_process.py"} <= selected)


class HistoryBuildCanonicalPin(unittest.TestCase):
    @unittest.skipUnless(sys.platform == "win32", "Actual Windows case-preserving filesystem handoff")
    def test_installed_lib_case_matches_the_real_capture_pin(self):
        capture = build._load_script(ROOT, "capture_shortcuts_history_ui")
        with tempfile.TemporaryDirectory(prefix="imagejxx-installed-pin-case-") as temporary:
            root = Path(temporary).resolve(strict=True)
            declared = root / "Lib/site-packages/fixture.pyd"
            declared.parent.mkdir(parents=True)
            declared.write_bytes(b"non-executable pin fixture\n")
            alternate = root / "lib/site-packages/fixture.pyd"
            self.assertNotEqual(str(declared), str(alternate))
            self.assertTrue(declared.samefile(alternate))
            expected = capture.pin(alternate)
            self.assertEqual(build._pin(alternate), expected)
            self.assertEqual(build._pin(declared), expected)
            self.assertEqual(capture.verify(build._pin(alternate)), expected)


class HistoryBuildSourceLoading(unittest.TestCase):
    def test_load_script_executes_source_despite_timestamp_valid_stale_bytecode(self):
        # Explicitly generated transport fixture, not a product/build invocation.
        # -B prevents writes; it does not prevent a legacy loader reading pyc.
        with tempfile.TemporaryDirectory(prefix="imagejxx-history-source-cache-") as temporary, \
                patch.object(sys, "dont_write_bytecode", True), \
                patch.object(sys, "pycache_prefix", None):
            root = Path(temporary).resolve(strict=True)
            source = root / "scripts/capture_shortcuts_history_ui.py"
            source.parent.mkdir()
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

            # Causal positive control: the former loader really executes the
            # stale payload, despite the current source having different bytes.
            spec = importlib.util.spec_from_file_location("history_stale_cache_control", source)
            legacy = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(legacy)
            self.assertEqual(legacy.SOURCE_MARKER, "stale")
            self.assertEqual(pin(source), source_pin)

            current = build._load_script(root, "capture_shortcuts_history_ui")
            self.assertEqual(current.SOURCE_MARKER, "fresh")
            self.assertEqual(Path(current.__file__).resolve(strict=True), source)
            self.assertEqual(pin(source), source_pin)
            self.assertEqual(source.read_bytes(), fresh)
            self.assertEqual(cache.read_bytes(), cached_bytes,
                             "The source-bound loader must not replace the stale-cache evidence")


if __name__ == "__main__":
    unittest.main()
