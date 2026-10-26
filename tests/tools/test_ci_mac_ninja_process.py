"""Bounded Ninja console-pool experiment, not product-build qualification.

Five native cases execute a real console edge; the sixth refuses a nonconsole
graph before execution. The original eight Python-only lifecycle cases are not
inherited. Run explicitly on Darwin with --require-darwin and an absolute
IMAGEJXX_NINJA_CONSOLE_CONTROL_EXE pointing to the reviewed PyPI Ninja 1.13.2
native binary. --print-tool resolves that same installed RECORD member on Darwin
without launching it; the native cases verify its exact banner through the owner.
ConsoleToolMetadata uses synthetic files and proves no Darwin ownership. No downloads,
compiler, CMake configuration, product imports or arbitrary graph admission.
"""
import base64
import csv
import hashlib
import importlib.metadata
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import shlex
import signal
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


BASE_PATH = Path(__file__).with_name("test_ci_mac_process.py")
SPEC = importlib.util.spec_from_file_location("mac_ninja_fixture", BASE_PATH)
base = importlib.util.module_from_spec(SPEC)
exec(compile(BASE_PATH.read_bytes(), str(BASE_PATH), "exec", dont_inherit=True), base.__dict__)
mac = base.mac
NINJA_ENV = "IMAGEJXX_NINJA_CONSOLE_CONTROL_EXE"
NINJA_VERSION = "1.13.2"
NINJA_BANNER = "1.13.2.git.kitware.jobserver-pipe-1"
REVIEWED_SOURCE = {
    "scope": "declared PyPI publishing/source provenance, not a runtime build claim",
    "packaging_commit": "dc4fad9143e6fd79f1fe6570037d9e0dfa8f5232",
    "ninja_gitlink": "7659b3d8f4f6204f6f9a1b3a5c9832334fa2834f",
    "upstream_base": "3441b633c2fe2c494e958780ba0f4227b1327634",
    "wheel": "ninja-1.13.2-py3-none-macosx_10_9_universal2.whl",
    "wheel_sha256": "fd82e26c0706ad4ab88e5fdd26f3fab0a987a90f810160f6c322e752c6af298b",
}
MACHO_MAGICS = {bytes.fromhex(value) for value in
                ("feedface", "cefaedfe", "feedfacf", "cffaedfe",
                 "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}


def require_native(*, tool_required=True):
    if sys.platform != "darwin":
        raise RuntimeError("Ninja console ownership requires the actual Darwin kernel")
    if tool_required and not os.environ.get(NINJA_ENV):
        raise RuntimeError("An explicit Ninja 1.13.2 executable is required")


def pin(path):
    path = Path(path).resolve(strict=True)
    info = path.stat()
    if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= 64 * 1024 * 1024:
        raise ValueError("Control input must be a bounded regular file")
    return {"path": str(path), "bytes": info.st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def resolve_tool(requested=None):
    """Read the selected distribution's native member; never search PATH/import ninja.

    The wheel hash is a reviewed publishing identity; the workflow independently
    pins installation to it. These checks bind installed metadata and native bytes,
    and deliberately make no reproducible-build claim from that metadata.
    """
    distribution = importlib.metadata.distribution("ninja")
    files = list(distribution.files or ())
    if not files or len(files) > 1024:
        raise ValueError("Ninja distribution requires a bounded original RECORD")
    metadata_rows = [str(item) for item in files
                     if item.name == "METADATA" and item.parent.name.endswith(".dist-info")]
    if len(metadata_rows) != 1:
        raise ValueError("Ninja distribution requires one original METADATA")
    metadata_name = metadata_rows[0]
    record_name = str(PurePosixPath(metadata_name).with_name("RECORD"))
    if sum(str(item) == record_name for item in files) != 1:
        raise ValueError("Ninja distribution requires one original RECORD")

    def original_file(logical, limit):
        path = Path(distribution.locate_file(logical))
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= limit:
            raise ValueError("Ninja original member must be a bounded regular file")
        return pin(path)

    record = original_file(record_name, 1024 * 1024)
    metadata_pin = original_file(metadata_name, 1024 * 1024)
    entries = {}
    for row in csv.reader(io.StringIO(Path(record["path"]).read_text(encoding="utf-8"))):
        if len(row) != 3 or not row[0] or row[0] in entries:
            raise ValueError("Ninja original RECORD has malformed or duplicate rows")
        entries[row[0]] = row[1:]
    if entries.get(record_name) != ["", ""]:
        raise ValueError("Ninja original RECORD self row must be unhashed")

    def verify_record(logical, member):
        encoded = base64.urlsafe_b64encode(bytes.fromhex(member["sha256"])).rstrip(b"=").decode("ascii")
        if entries.get(logical) != ["sha256=" + encoded, str(member["bytes"])]:
            raise ValueError("Ninja original RECORD hash/size differs: " + logical)

    verify_record(metadata_name, metadata_pin)
    metadata = distribution.metadata
    if metadata.get_all("Name") != ["ninja"] or metadata.get_all("Version") != [NINJA_VERSION]:
        raise ValueError("Only the exact ninja 1.13.2 distribution is admitted")
    native_rows = [name for name in entries if PurePosixPath(name).name == "ninja"]
    if len(native_rows) != 1:
        raise ValueError("Ninja original RECORD must own exactly one native basename ninja")
    native_name = native_rows[0]
    native = original_file(native_name, 64 * 1024 * 1024)
    verify_record(native_name, native)
    native_path = Path(native["path"])
    if not os.access(native_path, os.X_OK):
        raise ValueError("Ninja original native member must be executable")
    with native_path.open("rb") as stream:
        if stream.read(4) not in MACHO_MAGICS:
            raise ValueError("Ninja original native member must have Mach-O magic, not a wrapper")
    if requested is not None:
        candidate = Path(requested)
        if (not candidate.is_absolute() or candidate.resolve(strict=True) != native_path
                or not candidate.samefile(native_path)):
            raise ValueError("Explicit Ninja path differs from the original RECORD native member")
    return {"distribution": {"name": "ninja", "version": NINJA_VERSION},
            "record": record, "metadata": metadata_pin, "native": native,
            "native_record": {"path": native_name, "hash": entries[native_name][0],
                              "size": int(entries[native_name][1])},
            "expected_banner": NINJA_BANNER, "reviewed_source": dict(REVIEWED_SOURCE)}


def require_banner(observed):
    if observed != NINJA_BANNER:
        raise ValueError("Ninja banner differs from the exact reviewed PyPI 1.13.2 profile: "
                         + repr(observed[:256]))


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


class ConsoleToolMetadata(unittest.TestCase):
    """Synthetic distribution/file admission only; no native tool is executed."""

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx-ninja-tool-metadata-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve(strict=True)
        self.site = self.root / "site"
        self.dist_info = self.site / "ninja-1.13.2.dist-info"
        self.dist_info.mkdir(parents=True)
        self.binary = self.root / "bin" / "ninja"
        self.binary.parent.mkdir()
        self.binary.write_bytes(bytes.fromhex("cafebabe") + b"SYNTHETIC metadata only; not an executable")
        self.binary.chmod(0o755)
        self.metadata_file = self.dist_info / "METADATA"
        self.metadata_file.write_text("Metadata-Version: 2.1\nName: ninja\nVersion: 1.13.2\n", encoding="utf-8")
        self.record_file = self.dist_info / "RECORD"
        self.binary_name = "../bin/ninja"
        self.metadata_name = "ninja-1.13.2.dist-info/METADATA"
        self.record_name = "ninja-1.13.2.dist-info/RECORD"
        self.write_record()
        self.distribution = importlib.metadata.PathDistribution(self.dist_info)
        provider = patch.object(importlib.metadata, "distribution", return_value=self.distribution)
        self.provider = provider.start()
        self.addCleanup(provider.stop)

    def write_record(self, *, hash_override=None, size_override=None, extra_rows=()):
        rows = []
        for name, path in ((self.binary_name, self.binary), (self.metadata_name, self.metadata_file)):
            value = path.read_bytes()
            digest = "sha256=" + base64.urlsafe_b64encode(hashlib.sha256(value).digest()).rstrip(b"=").decode("ascii")
            size = str(len(value))
            if name == self.binary_name:
                digest = digest if hash_override is None else hash_override
                size = size if size_override is None else size_override
            rows.append([name, digest, size])
        rows.extend(extra_rows)
        rows.append([self.record_name, "", ""])
        with self.record_file.open("w", encoding="utf-8", newline="") as stream:
            csv.writer(stream).writerows(rows)

    def test_original_record_selects_native_and_retains_exact_profile_pins(self):
        proof = resolve_tool(str(self.binary))
        self.provider.assert_called_once_with("ninja")
        self.assertEqual(proof["native"], pin(self.binary))
        self.assertEqual(proof["record"], pin(self.record_file))
        self.assertEqual(proof["metadata"], pin(self.metadata_file))
        self.assertEqual(proof["distribution"], {"name": "ninja", "version": "1.13.2"})
        self.assertEqual(proof["expected_banner"], "1.13.2.git.kitware.jobserver-pipe-1")
        self.assertEqual(proof["reviewed_source"]["packaging_commit"],
                         "dc4fad9143e6fd79f1fe6570037d9e0dfa8f5232")
        self.assertEqual(proof["reviewed_source"]["ninja_gitlink"],
                         "7659b3d8f4f6204f6f9a1b3a5c9832334fa2834f")
        self.assertIn("not a runtime build claim", proof["reviewed_source"]["scope"])

    def test_other_distribution_name_or_version_refuses_even_with_valid_record(self):
        for name, version in (("ninja", "1.13.1"), ("ninja", "1.13.2.post1"), ("other", "1.13.2")):
            with self.subTest(name=name, version=version):
                self.metadata_file.write_text("Name: " + name + "\nVersion: " + version + "\n", encoding="utf-8")
                self.write_record()
                with self.assertRaisesRegex(ValueError, "exact ninja 1.13.2"):
                    resolve_tool()

    def test_changed_native_bytes_refuse_original_record_hash(self):
        self.binary.write_bytes(self.binary.read_bytes()[:-1] + b"!")
        with self.assertRaisesRegex(ValueError, "RECORD hash/size differs"):
            resolve_tool()

    def test_wrong_record_size_and_noncanonical_size_refuse(self):
        for size in (str(self.binary.stat().st_size + 1), "0" + str(self.binary.stat().st_size)):
            with self.subTest(size=size):
                self.write_record(size_override=size)
                with self.assertRaisesRegex(ValueError, "RECORD hash/size differs"):
                    resolve_tool()

    def test_wrong_record_hash_algorithm_refuses(self):
        self.write_record(hash_override="sha512=declared-but-unreviewed")
        with self.assertRaisesRegex(ValueError, "RECORD hash/size differs"):
            resolve_tool()

    def test_script_wrapper_refuses_even_when_record_hash_is_valid(self):
        self.binary.write_bytes(b"#!/usr/bin/env python3\nfrom ninja import ninja\nninja()\n")
        self.write_record()
        with self.assertRaisesRegex(ValueError, "Mach-O magic, not a wrapper"):
            resolve_tool()

    def test_foreign_or_relative_explicit_path_refuses(self):
        foreign = self.root / "foreign-ninja"
        foreign.write_bytes(self.binary.read_bytes())
        for path in (str(foreign), "ninja"):
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "Explicit Ninja path differs"):
                resolve_tool(path)

    def test_ambiguous_record_native_members_refuse(self):
        self.write_record(extra_rows=[["other/ninja", "sha256=unused", "1"]])
        with self.assertRaisesRegex(ValueError, "exactly one native basename"):
            resolve_tool()

    def test_nonexecutable_native_refuses(self):
        with patch.object(os, "access", return_value=False), self.assertRaisesRegex(ValueError, "must be executable"):
            resolve_tool()

    def test_metadata_mutation_refuses_original_record(self):
        self.metadata_file.write_text("Name: ninja\nVersion: 1.13.2\nSummary: changed\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "RECORD hash/size differs"):
            resolve_tool()

    def test_exact_banner_accepts_no_prefix_or_alternate_build(self):
        require_banner("1.13.2.git.kitware.jobserver-pipe-1")
        for banner in ("1.13.2", "1.13.2.git.kitware.jobserver-pipe-2",
                       "1.13.2.git.kitware.jobserver-pipe-1-extra"):
            with self.subTest(banner=banner), self.assertRaisesRegex(ValueError, "exact reviewed"):
                require_banner(banner)


@unittest.skipUnless(sys.platform == "darwin" and bool(os.environ.get(NINJA_ENV)),
                     "Explicit actual-Darwin Ninja 1.13.2 experiment; metadata is not native evidence")
class DarwinNinjaLifecycle(unittest.TestCase):
    tool_proof = None

    def setUp(self):
        base.DarwinLifecycle.setUp(self)
        self.addCleanup(self.report_control)
        require_native()
        self.tool_before = resolve_tool(os.environ[NINJA_ENV])
        self.ninja_pin = self.tool_before["native"]
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
            require_banner((self.root / "version.log").read_text(encoding="ascii").strip())
            type(self).tool_proof = {"pin": self.ninja_pin, "version": NINJA_BANNER,
                                    "admission": self.tool_before, "process": row}
        self.assertEqual(self.ninja_pin, type(self).tool_proof["pin"])
        self.assertEqual(self.tool_before, type(self).tool_proof["admission"])
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
        after_error = None
        if hasattr(self, "tool_before"):
            evidence["tool_before"] = self.tool_before
            try:
                evidence["tool_after"] = resolve_tool(os.environ[NINJA_ENV])
            except Exception as error:
                after_error = error
                evidence["tool_after_error"] = {"type": type(error).__name__, "message": str(error)}
        print("NINJA_CONSOLE_CONTROL " + json.dumps(evidence, sort_keys=True), flush=True)
        if after_error is not None:
            raise after_error
        if hasattr(self, "tool_before"):
            self.assertEqual(evidence["tool_after"], self.tool_before)
            self.assertEqual(pin(self.ninja), self.ninja_pin)

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
        # Pinned 7659b3d8 build.cc:986-990 / ninja.cc:1766-1769 return the job's exact status.
        self.assertEqual((row["state"], row["exit_code"], row["target_completed"]), ("exited", code, True))
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


class ConsoleExitMetadata(unittest.TestCase):
    """Synthetic rows exercise the actual assertions; no Darwin setup or process."""

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="imagejxx-ninja-exit-metadata-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve(strict=True)

    def invoke(self, code, *, observed_code=None, edge_changes=None):
        fixture = DarwinNinjaLifecycle("test_console_normal_exit")
        fixture.root = self.root
        fixture.ninja = str(self.root / "synthetic-ninja")
        fixture.receipt = self.root / "synthetic-owned.json"
        fixture.nonconsole = False
        row = {"state": "exited", "exit_code": code if observed_code is None else observed_code,
               "target_pid": 101, "target_completed": True, "cleanup_complete": True,
               "timed_out": False, "descendants_remained_after_target_completion": False,
               "ownership": {"method": "macos-cooperative-broker", "session_id": 100,
                             "supervisor_pid": 100, "untrusted_containment": False,
                             "sigchld_default": True, "pending_bootstraps": [],
                             "groups": [{"pid": 101, "anchor_pid": 102, "cleanup_complete": True,
                                         "cleanup": {"terminal_wait": "group_drained_before_anchor_reap"}}]},
               "cleanup": {"remaining": []}}
        edge = {"pid": 103, "parent_pid": 101, "pgid": 101, "sid": 100, "payload_exit": code}
        edge.update(edge_changes or {})

        def synthetic_fixture(source, *, launcher):
            self.assertIn("sys.exit(" + repr(code) + ")", source)
            row["argv"] = launcher(["/synthetic/python", "-I", "/synthetic/payload.py"])
            fixture.receipt.write_text(json.dumps(row), encoding="utf-8")
            (self.root / "edge.json").write_text(json.dumps(edge), encoding="utf-8")
            return row

        with patch.object(fixture, "run_fixture", side_effect=synthetic_fixture) as launch:
            try:
                fixture.run_exit(code)
            finally:
                launch.assert_called_once()

    def test_actual_run_exit_accepts_only_the_exact_zero_and_seven_rows(self):
        for code in (0, 7):
            with self.subTest(code=code):
                self.invoke(code)

    def test_actual_run_exit_refuses_one_for_zero_or_seven(self):
        for code in (0, 7):
            with self.subTest(code=code), self.assertRaises(AssertionError):
                self.invoke(code, observed_code=1)

    def test_actual_run_exit_refuses_changed_edge_identity(self):
        for field, value in (("pid", 101), ("parent_pid", 104), ("pgid", 104),
                             ("sid", 104), ("payload_exit", 1)):
            with self.subTest(field=field), self.assertRaises(AssertionError):
                self.invoke(7, edge_changes={field: value})


if __name__ == "__main__":
    if "--print-tool" in sys.argv[1:]:
        if sys.argv[1:] != ["--print-tool"]:
            raise SystemExit("The tool selector accepts only --print-tool")
        require_native(tool_required=False)
        print(resolve_tool()["native"]["path"])
        raise SystemExit(0)
    if "--require-darwin" in sys.argv[1:]:
        if sys.argv[1:] != ["--require-darwin"]:
            raise SystemExit("The native selector accepts only --require-darwin")
        require_native()
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(DarwinNinjaLifecycle))
        raise SystemExit(0 if result.wasSuccessful() and result.testsRun == 6 and not result.skipped else 1)
    unittest.main()
