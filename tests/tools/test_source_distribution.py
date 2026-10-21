"""Isolated safety tests for the source-only distribution builder.

Fixtures and injected backends are owned test data. The optional installed-backend
smoke case uses scikit-build-core==0.11.6; no product import, CMake, repository build
tree, or backend download is needed.
"""

from contextlib import contextmanager
from dataclasses import FrozenInstanceError
from email.parser import BytesParser
import hashlib
import importlib
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import sys
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


ROOT_FILES = (".gitignore", "CMakeLists.txt", "pyproject.toml", "README.md",
              "LICENSE", "THIRD_PARTY_NOTICES.md")
SOURCE_SUBTREES = ("include", "src", "tests", "examples", "scripts", "docs",
                   "licenses", "cmake", ".github", "third_party")
PREFIX = "imagejxx-0.2.0"
ARCHIVE = PREFIX + ".tar.gz"
PKG_INFO = b"Metadata-Version: 2.2\nName: imagejxx\nVersion: 0.2.0\n\n"
PYPROJECT = b'''[build-system]
requires = ["scikit-build-core==0.11.6"]
build-backend = "scikit_build_core.build"

[project]
name = "imagejxx"
version = "0.2.0"
description = "Owned source distribution fixture"
readme = "README.md"
license = {file = "LICENSE"}
'''
FIXTURE = {
    ".gitignore": b"build/\n__pycache__/\n*.so\n",
    "CMakeLists.txt": b"cmake_minimum_required(VERSION 3.24)\n",
    "pyproject.toml": PYPROJECT,
    "README.md": b"# Owned source fixture\n",
    "LICENSE": b"Owned test license text\n",
    "THIRD_PARTY_NOTICES.md": b"Owned test dependency notices\n",
    "include/imagejxx/probe.hpp": b"#pragma once\n",
    "src/native/probe.cpp": b"int owned_probe() { return 7; }\n",
    "tests/native/probe.cpp": b"// owned test source\n",
    "examples/cpp/probe.cpp": b"// owned example source\n",
    "scripts/probe.py": b"# owned script source\n",
    "docs/probe.md": b"Owned source documentation\n",
    "licenses/dependencies.json": b'{"dependencies": []}\n',
    "cmake/probe.cmake": b"# owned CMake source\n",
    ".github/workflows/probe.yml": b"name: owned source fixture\n",
    "third_party/probe.hpp": b"// owned vendored source fixture\n",
}


def _normal(path):
    return os.path.normcase(os.path.abspath(os.fsdecode(os.fspath(path))))


def _within(path, root):
    candidate, parent = _normal(path), _normal(root)
    return candidate == parent or candidate.startswith(parent + os.sep)


@contextmanager
def _forbid_tree_inspection(blocked, *, unlisted=()):
    """Reject descent and metadata reads, including via DirEntry methods."""
    real_scandir, real_lstat = os.scandir, os.lstat
    real_path_lstat = Path.lstat
    visited = []

    def allowed(path):
        if not isinstance(path, int):
            if any(_within(path, directory) for directory in blocked):
                raise AssertionError("A pruned source tree was inspected: " + os.fspath(path))

    class Entry:
        def __init__(self, entry):
            self.entry = entry
            self.name, self.path = entry.name, entry.path

        def __fspath__(self):
            return self.path

        def stat(self, *args, **kwargs):
            allowed(self.path)
            return self.entry.stat(*args, **kwargs)

        def is_symlink(self):
            allowed(self.path)
            return self.entry.is_symlink()

        def is_dir(self, *args, **kwargs):
            allowed(self.path)
            return self.entry.is_dir(*args, **kwargs)

        def is_file(self, *args, **kwargs):
            allowed(self.path)
            return self.entry.is_file(*args, **kwargs)

        def inode(self):
            allowed(self.path)
            return self.entry.inode()

    class Scan:
        def __init__(self, iterator):
            self.iterator = iterator

        def __iter__(self):
            return self

        def __next__(self):
            return Entry(next(self.iterator))

        def __enter__(self):
            return self

        def __exit__(self, *unused):
            self.close()

        def close(self):
            self.iterator.close()

    def scandir(path):
        allowed(path)
        if any(_normal(path) == _normal(directory) for directory in unlisted):
            raise AssertionError("The positive source root was enumerated")
        visited.append(os.fspath(path))
        return Scan(real_scandir(path))

    def lstat(path, *args, **kwargs):
        allowed(path)
        return real_lstat(path, *args, **kwargs)

    def path_lstat(path, *args, **kwargs):
        allowed(path)
        return real_path_lstat(path, *args, **kwargs)

    with mock.patch.object(os, "scandir", new=scandir), \
            mock.patch.object(os, "lstat", new=lstat), \
            mock.patch.object(Path, "lstat", new=path_lstat), \
            mock.patch.object(os, "walk", side_effect=AssertionError("Source collection must not use os.walk")):
        yield visited


class _StatOverride:
    def __init__(self, original, *, mode=None, attributes=None):
        self.original = original
        self.st_mode = original.st_mode if mode is None else mode
        self.st_file_attributes = (getattr(original, "st_file_attributes", 0)
                                   if attributes is None else attributes)

    def __getattr__(self, name):
        return getattr(self.original, name)

    def __getitem__(self, index):
        return self.st_mode if index == stat.ST_MODE else self.original[index]


@contextmanager
def _unsafe_lstat(path, *, mode=None, attributes=None):
    """Portable symlink/reparse simulation, without Windows link privileges."""
    real_path_lstat, real_lstat, real_open = Path.lstat, os.lstat, Path.open
    real_os_open = os.open

    def wrap(candidate, result):
        return _StatOverride(result, mode=mode, attributes=attributes) if _normal(candidate) == _normal(path) else result

    def path_lstat(candidate, *args, **kwargs):
        return wrap(candidate, real_path_lstat(candidate, *args, **kwargs))

    def lstat(candidate, *args, **kwargs):
        return wrap(candidate, real_lstat(candidate, *args, **kwargs))

    def opened(candidate, *args, **kwargs):
        if _normal(candidate) == _normal(path):
            raise AssertionError("Unsafe source entry was opened before refusal")
        return real_open(candidate, *args, **kwargs)

    def os_opened(candidate, *args, **kwargs):
        if _normal(candidate) == _normal(path):
            raise AssertionError("Unsafe source entry reached os.open before refusal")
        return real_os_open(candidate, *args, **kwargs)

    with mock.patch.object(Path, "lstat", new=path_lstat), \
            mock.patch.object(os, "lstat", new=lstat), \
            mock.patch.object(Path, "open", new=opened), \
            mock.patch.object(os, "open", new=os_opened):
        yield


class _Backend:
    __version__ = "0.11.6"

    def __init__(self, *, mutate=None, failure=None, written_pkg_info=PKG_INFO, extra_member=None):
        self.mutate, self.failure = mutate, failure
        self.written_pkg_info, self.extra_member = written_pkg_info, extra_member
        self.calls = 0
        self.cwd = None
        self.paths = None
        self.output_directory = None

        def original_discovery(*args, **kwargs):
            raise AssertionError("Unpinned backend discovery was invoked")

        self.original_discovery = original_discovery
        self.each_unignored_file = original_discovery

    def expected_pkg_info(self, pyproject):
        assert pyproject["project"]["name"] == "imagejxx"
        assert pyproject["project"]["version"] == "0.2.0"
        return PKG_INFO

    def build_sdist(self, output_directory):
        self.calls += 1
        self.cwd = Path.cwd()
        self.output_directory = Path(output_directory)
        paths = list(self.each_unignored_file(Path(".")))
        assert paths == list(self.each_unignored_file(Path("."), "ignored-filter", arbitrary="ignored"))
        self.paths = tuple(Path(path).as_posix() for path in paths)
        assert all(not Path(path).is_absolute() and ".." not in Path(path).parts for path in paths)
        if self.failure is not None:
            raise self.failure
        with tarfile.open(self.output_directory / ARCHIVE, "w:gz") as archive:
            for relative in paths:
                payload = Path(relative).read_bytes()
                member = tarfile.TarInfo(PREFIX + "/" + Path(relative).as_posix())
                member.size, member.mtime = len(payload), 0
                archive.addfile(member, io.BytesIO(payload))
            member = tarfile.TarInfo(PREFIX + "/PKG-INFO")
            member.size, member.mtime = len(self.written_pkg_info), 0
            archive.addfile(member, io.BytesIO(self.written_pkg_info))
            if self.extra_member is not None:
                name, contents = self.extra_member
                member = tarfile.TarInfo(PREFIX + "/" + name)
                member.size, member.mtime = len(contents), 0
                archive.addfile(member, io.BytesIO(contents))
        if self.mutate is not None:
            self.mutate()
        return ARCHIVE


class SourceDistributionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        script = Path(__file__).resolve().parents[2] / "scripts" / "build_source_distribution.py"
        name = "_imagejxx_source_distribution_under_test"
        spec = importlib.util.spec_from_file_location(name, script)
        cls.module = importlib.util.module_from_spec(spec)
        previous_module = sys.modules.get(name)
        sys.modules[name] = cls.module  # dataclasses resolves its module here.
        previous_bytecode = sys.dont_write_bytecode
        try:
            sys.dont_write_bytecode = True
            spec.loader.exec_module(cls.module)  # __name__ is never __main__.
        finally:
            sys.dont_write_bytecode = previous_bytecode

        def restore_module():
            if previous_module is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = previous_module

        cls.addClassCleanup(restore_module)

    def setUp(self):
        owned = tempfile.TemporaryDirectory(prefix="imagejxx-source-distribution-test-")
        self.addCleanup(owned.cleanup)
        self.directory = Path(owned.name)
        self.root, self.output = self.directory / "source", self.directory / "out"
        self.root.mkdir()
        self.report = self.output / "receipt.json"
        for relative, contents in FIXTURE.items():
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)
        clean_environment = {key: value for key, value in os.environ.items()
                             if not key.upper().startswith("SKBUILD_")}
        environment = mock.patch.dict(os.environ, clean_environment, clear=True)
        environment.start()
        self.addCleanup(environment.stop)

    def assert_no_published_output(self):
        self.assertFalse((self.output / ARCHIVE).exists())
        self.assertFalse(self.report.exists())
        if self.output.exists():
            self.assertEqual(list(self.output.iterdir()), [], "Private archive staging was left behind")

    def test_positive_collector_is_sorted_pinned_and_frozen(self):
        self.assertEqual(tuple(self.module.ROOT_FILES), ROOT_FILES)
        self.assertEqual(tuple(self.module.SOURCE_SUBTREES), SOURCE_SUBTREES)
        records = self.module.collect_sources(self.root)
        self.assertIsInstance(records, tuple)
        self.assertEqual(tuple(record.relative_path for record in records), tuple(sorted(FIXTURE)))
        for record in records:
            self.assertIsInstance(record, self.module.FrozenFile)
            payload = FIXTURE[record.relative_path]
            self.assertEqual(record.size, len(payload))
            self.assertEqual(record.sha256, hashlib.sha256(payload).hexdigest())
            self.assertIsInstance(record.stat_identity, tuple)
            self.assertTrue(record.stat_identity)
        with self.assertRaises(FrozenInstanceError):
            records[0].size = 0

    def test_root_build_and_nested_ignore_discovery_are_never_walked(self):
        blocked = self.root / "build"
        trap = blocked / "unreadable" / ".gitignore"
        trap.parent.mkdir(parents=True)
        trap.write_bytes(b"**\n")
        backend = _Backend()
        previous_cwd = Path.cwd()
        with _forbid_tree_inspection((blocked,), unlisted=(self.root,)) as visited:
            receipt = self.module.build_source_distribution(
                self.root, self.output, self.report, _backend=backend)
        self.assertTrue(receipt["success"])
        self.assertFalse(any(_within(path, blocked) for path in visited))
        self.assertEqual(backend.paths, tuple(sorted(FIXTURE)))
        self.assertEqual(backend.cwd, self.root)
        self.assertEqual(Path.cwd(), previous_cwd)
        self.assertIs(backend.each_unignored_file, backend.original_discovery)

    def test_generated_cache_and_binary_entries_are_pruned_before_inspection(self):
        blocked = tuple(self.root / name for name in (
            "src/build", "src/__pycache__", "docs/.pytest_cache", "include/widget.egg-info"))
        for path in blocked:
            (path / "unreadable").mkdir(parents=True)
            (path / "unreadable" / ".gitignore").write_bytes(b"**\n")
        for relative in ("src/generated.pyc", "include/probe.pyd", "src/probe.so", "docs/Thumbs.db"):
            (self.root / relative).write_bytes(b"generated fixture junk\n")
        with _forbid_tree_inspection(blocked):
            records = self.module.collect_sources(self.root)
        self.assertEqual(tuple(record.relative_path for record in records), tuple(sorted(FIXTURE)))

    def test_symlink_lstat_refused_before_payload_open(self):
        target = self.root / "src/native/probe.cpp"
        with _unsafe_lstat(target, mode=stat.S_IFLNK | 0o777):
            with self.assertRaises(self.module.SourceDistributionError):
                self.module.collect_sources(self.root)

    def test_windows_reparse_lstat_refused_before_payload_open(self):
        target = self.root / "src/native/probe.cpp"
        # FILE_ATTRIBUTE_REPARSE_POINT is0x400, simulated on every host OS.
        with _unsafe_lstat(target, attributes=0x400):
            with self.assertRaises(self.module.SourceDistributionError):
                self.module.collect_sources(self.root)

    def test_exact_collection_caps_accept_and_minus_one_caps_refuse(self):
        count, size = len(FIXTURE), sum(map(len, FIXTURE.values()))
        self.assertEqual(len(self.module.collect_sources(self.root, max_files=count, max_bytes=size)), count)
        with self.assertRaises(self.module.SourceDistributionError):
            self.module.collect_sources(self.root, max_files=count - 1, max_bytes=size)
        with self.assertRaises(self.module.SourceDistributionError):
            self.module.collect_sources(self.root, max_files=count, max_bytes=size - 1)

    def test_backend_archive_and_receipt_match_independent_fixture_bytes(self):
        backend = _Backend()
        previous_cwd = Path.cwd()
        receipt = self.module.build_source_distribution(
            self.root, self.output, self.report, _backend=backend)
        self.assertEqual(backend.calls, 1)
        self.assertEqual(backend.paths, tuple(sorted(FIXTURE)))
        self.assertEqual(Path.cwd(), previous_cwd)
        self.assertIs(backend.each_unignored_file, backend.original_discovery)
        self.assertTrue(_within(backend.output_directory, self.output))
        self.assertNotEqual(backend.output_directory, self.output)
        self.assertFalse(backend.output_directory.exists())
        archive = self.output / ARCHIVE
        payload = archive.read_bytes()
        self.assertEqual(receipt["schema_version"], 1)
        self.assertIs(receipt["success"], True)
        self.assertEqual(Path(receipt["archive"]["path"]), archive)
        self.assertEqual(receipt["archive"]["size"], len(payload))
        self.assertEqual(receipt["archive"]["sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(receipt["source_files"], [
            {"path": path, "size": len(FIXTURE[path]), "sha256": hashlib.sha256(FIXTURE[path]).hexdigest()}
            for path in sorted(FIXTURE)])
        self.assertEqual(receipt["pkg_info"], {
            "path": PREFIX + "/PKG-INFO", "size": len(PKG_INFO),
            "sha256": hashlib.sha256(PKG_INFO).hexdigest()})
        self.assertEqual(receipt["backend"]["version"], "0.11.6")
        self.assertEqual(receipt["collection"]["root_files"], list(ROOT_FILES))
        self.assertEqual(receipt["collection"]["subtrees"], list(SOURCE_SUBTREES))
        self.assertEqual(receipt["collection"]["max_files"], 50000)
        self.assertEqual(receipt["collection"]["max_bytes"], 268435456)
        self.assertEqual(json.loads(self.report.read_text(encoding="utf-8")), receipt)
        with tarfile.open(archive, "r:gz") as packed:
            members = packed.getmembers()
            self.assertEqual({member.name for member in members},
                             {PREFIX + "/" + path for path in FIXTURE} | {PREFIX + "/PKG-INFO"})
            self.assertTrue(all(member.isfile() for member in members))
            for member in members:
                expected = PKG_INFO if member.name == PREFIX + "/PKG-INFO" else FIXTURE[member.name[len(PREFIX) + 1:]]
                self.assertEqual(packed.extractfile(member).read(), expected)
        self.assertEqual(sorted(path.name for path in self.output.iterdir()), [ARCHIVE, self.report.name])

    def test_source_mutation_after_backend_stops_archive_and_receipt(self):
        source = self.root / "src/native/probe.cpp"
        original = source.read_bytes()
        backend = _Backend(mutate=lambda: source.write_bytes(b"X" * len(original)))
        previous_cwd = Path.cwd()
        with self.assertRaises(self.module.SourceDistributionError):
            self.module.build_source_distribution(self.root, self.output, self.report, _backend=backend)
        self.assertEqual(backend.calls, 1)
        self.assertNotEqual(source.read_bytes(), original)
        self.assertEqual(Path.cwd(), previous_cwd)
        self.assertIs(backend.each_unignored_file, backend.original_discovery)
        self.assert_no_published_output()

    def test_existing_archive_is_unchanged_and_backend_is_not_called(self):
        self.output.mkdir()
        archive = self.output / ARCHIVE
        archive.write_bytes(b"owned existing archive\n")
        backend = _Backend()
        with self.assertRaises(self.module.SourceDistributionError):
            self.module.build_source_distribution(self.root, self.output, self.report, _backend=backend)
        self.assertEqual(archive.read_bytes(), b"owned existing archive\n")
        self.assertFalse(self.report.exists())
        self.assertEqual(backend.calls, 0)
        self.assertIs(backend.each_unignored_file, backend.original_discovery)

    def test_existing_report_is_unchanged_and_backend_is_not_called(self):
        self.output.mkdir()
        self.report.write_bytes(b"owned existing report\n")
        backend = _Backend()
        with self.assertRaises(self.module.SourceDistributionError):
            self.module.build_source_distribution(self.root, self.output, self.report, _backend=backend)
        self.assertEqual(self.report.read_bytes(), b"owned existing report\n")
        self.assertFalse((self.output / ARCHIVE).exists())
        self.assertEqual(backend.calls, 0)

    def test_unpinned_backend_version_is_refused_before_backend_execution(self):
        backend = _Backend()
        backend.__version__ = "0.11.5"
        with self.assertRaises(self.module.SourceDistributionError):
            self.module.build_source_distribution(self.root, self.output, self.report, _backend=backend)
        self.assertEqual(backend.calls, 0)
        self.assertIs(backend.each_unignored_file, backend.original_discovery)
        self.assert_no_published_output()

    def test_backend_failure_restores_collector_cwd_and_owned_staging(self):
        backend = _Backend(failure=RuntimeError("owned backend failure"))
        previous_cwd = Path.cwd()
        with self.assertRaisesRegex(RuntimeError, "owned backend failure"):
            self.module.build_source_distribution(self.root, self.output, self.report, _backend=backend)
        self.assertEqual(backend.calls, 1)
        self.assertEqual(Path.cwd(), previous_cwd)
        self.assertIs(backend.each_unignored_file, backend.original_discovery)
        self.assert_no_published_output()

    def test_corrupt_pkg_info_or_unexpected_member_refuses_publication(self):
        variants = (
            ("pkg-info-bytes", {"written_pkg_info": PKG_INFO.replace(b"0.2.0", b"9.9.9")}),
            ("unexpected-member", {"extra_member": ("uncollected.txt", b"not a frozen source\n")}),
        )
        for label, options in variants:
            with self.subTest(variant=label):
                backend = _Backend(**options)
                previous_cwd = Path.cwd()
                with self.assertRaises(self.module.SourceDistributionError):
                    self.module.build_source_distribution(self.root, self.output, self.report, _backend=backend)
                self.assertEqual(backend.calls, 1)
                self.assertEqual(backend.paths, tuple(sorted(FIXTURE)))
                self.assertEqual(Path.cwd(), previous_cwd)
                self.assertIs(backend.each_unignored_file, backend.original_discovery)
                self.assert_no_published_output()

    def test_unsafe_source_hooks_are_refused_before_backend_call(self):
        variants = (
            ("dynamic", PYPROJECT.replace(b"[project]\n", b'[project]\ndynamic = ["version"]\n'), {}),
            ("generate", PYPROJECT + b'\n[tool.scikit-build]\ngenerate = [{path = "owned.txt", template = "owned"}]\n', {}),
            ("metadata", PYPROJECT + b'\n[tool.scikit-build.metadata.version]\nprovider = "owned.fixture"\n', {}),
            ("overrides", PYPROJECT + b'\n[[tool.scikit-build.overrides]]\nif.platform-system = "Windows"\n', {}),
            ("sdist-cmake", PYPROJECT + b"\n[tool.scikit-build.sdist]\ncmake = true\n", {}),
            ("backend-path", PYPROJECT.replace(b'build-backend = "scikit_build_core.build"\n',
                b'build-backend = "scikit_build_core.build"\nbackend-path = ["owned-backend"]\n'), {}),
            ("environment", PYPROJECT, {"SKBUILD_SDIST_CMAKE": "1"}),
        )
        project = self.root / "pyproject.toml"
        try:
            for label, payload, environment in variants:
                with self.subTest(variant=label):
                    project.write_bytes(payload)
                    backend = _Backend()
                    previous_cwd = Path.cwd()
                    with mock.patch.dict(os.environ, environment, clear=False):
                        with self.assertRaises(self.module.SourceDistributionError):
                            self.module.build_source_distribution(self.root, self.output, self.report, _backend=backend)
                    self.assertEqual(backend.calls, 0)
                    self.assertEqual(Path.cwd(), previous_cwd)
                    self.assertIs(backend.each_unignored_file, backend.original_discovery)
                    self.assert_no_published_output()
        finally:
            project.write_bytes(PYPROJECT)

    def test_installed_pinned_backend_never_discovers_unlisted_source_trees(self):
        try:
            backend = importlib.import_module("scikit_build_core.build.sdist")
        except ModuleNotFoundError as error:
            if error.name == "scikit_build_core":
                self.skipTest("Optional installed scikit-build-core backend is unavailable")
            raise
        self.assertEqual(backend.__version__, "0.11.6", "Installed backend must match the qualified pin")
        original_discovery = backend.each_unignored_file
        previous_cwd = Path.cwd()
        blocked = self.root / "build"
        trap = blocked / "unreadable" / ".gitignore"
        trap.parent.mkdir(parents=True)
        trap.write_bytes(b"**\n")
        try:
            with _forbid_tree_inspection((blocked,), unlisted=(self.root,)) as visited:
                receipt = self.module.build_source_distribution(self.root, self.output, self.report)
        finally:
            self.assertEqual(Path.cwd(), previous_cwd)
            self.assertIs(backend.each_unignored_file, original_discovery)
        self.assertFalse(any(_within(path, blocked) for path in visited))
        self.assertIs(receipt["success"], True)
        self.assertEqual(receipt["backend"]["version"], "0.11.6")
        pins = receipt["backend"]["source_pins"]
        self.assertEqual(len(pins), 2)
        self.assertEqual({Path(pin["path"]).name for pin in pins}, {"sdist.py", "_file_processor.py"})
        for pin in pins:
            payload = Path(pin["path"]).read_bytes()
            self.assertGreater(len(payload), 0)
            self.assertEqual(pin["size"], len(payload))
            self.assertEqual(pin["sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(receipt["source_files"], [
            {"path": path, "size": len(FIXTURE[path]), "sha256": hashlib.sha256(FIXTURE[path]).hexdigest()}
            for path in sorted(FIXTURE)])
        archive = self.output / ARCHIVE
        archive_bytes = archive.read_bytes()
        self.assertEqual(receipt["archive"], {
            "path": str(archive), "size": len(archive_bytes),
            "sha256": hashlib.sha256(archive_bytes).hexdigest()})
        with tarfile.open(archive, "r:gz") as packed:
            members = packed.getmembers()
            self.assertEqual(len(members), len(FIXTURE) + 1)
            self.assertEqual({member.name for member in members},
                             {PREFIX + "/" + path for path in FIXTURE} | {PREFIX + "/PKG-INFO"})
            self.assertTrue(all(member.isfile() for member in members))
            for path, expected in FIXTURE.items():
                self.assertEqual(packed.extractfile(PREFIX + "/" + path).read(), expected)
            metadata_bytes = packed.extractfile(PREFIX + "/PKG-INFO").read()
        metadata = BytesParser().parsebytes(metadata_bytes)
        self.assertEqual(metadata["Name"], "imagejxx")
        self.assertEqual(metadata["Version"], "0.2.0")
        self.assertEqual(metadata["Summary"], "Owned source distribution fixture")
        self.assertEqual(metadata["Description-Content-Type"], "text/markdown")
        self.assertIn(FIXTURE["README.md"].decode("utf-8"), metadata.get_payload())
        self.assertEqual(receipt["pkg_info"], {
            "path": PREFIX + "/PKG-INFO", "size": len(metadata_bytes),
            "sha256": hashlib.sha256(metadata_bytes).hexdigest()})
        self.assertEqual(json.loads(self.report.read_text(encoding="utf-8")), receipt)
        self.assertEqual(sorted(path.name for path in self.output.iterdir()), [ARCHIVE, self.report.name])

    def test_direct_identity_uses_stable_windows_birthtime_and_posix_ctime(self):
        values = {
            "st_dev": 11, "st_ino": 22, "st_mode": 33188, "st_size": 44,
            "st_mtime_ns": 55, "st_ctime_ns": 66, "st_birthtime_ns": 77,
        }
        initial = SimpleNamespace(**values)
        changed_ctime = SimpleNamespace(**dict(values, st_ctime_ns=99))
        changes = (
            ("st_dev", 111, (111, 22, 33188, 44, 55, 77)),
            ("st_ino", 222, (11, 222, 33188, 44, 55, 77)),
            ("st_mode", 33216, (11, 22, 33216, 44, 55, 77)),
            ("st_size", 444, (11, 22, 33188, 444, 55, 77)),
            ("st_mtime_ns", 555, (11, 22, 33188, 44, 555, 77)),
            ("st_birthtime_ns", 777, (11, 22, 33188, 44, 55, 777)),
        )
        changed_views = tuple(SimpleNamespace(**dict(values, **{field: value}))
                              for field, value, unused_expected in changes)
        no_birthtime = SimpleNamespace(st_dev=11, st_ino=22, st_mode=33188,
                                      st_size=44, st_mtime_ns=55, st_ctime_ns=66)
        no_birthtime_changed = SimpleNamespace(st_dev=11, st_ino=22, st_mode=33188,
                                              st_size=44, st_mtime_ns=55, st_ctime_ns=99)
        # Keep the process-wide os.name patch around pure tuple extraction only.
        with mock.patch.object(self.module.os, "name", "nt"):
            windows_initial = self.module._identity(initial)
            windows_changed_ctime = self.module._identity(changed_ctime)
            windows_changes = tuple(self.module._identity(view) for view in changed_views)
            windows_fallback = self.module._identity(no_birthtime)
            windows_fallback_changed = self.module._identity(no_birthtime_changed)
        with mock.patch.object(self.module.os, "name", "posix"):
            posix_initial = self.module._identity(initial)
            posix_changed_ctime = self.module._identity(changed_ctime)
            posix_changed_birthtime = self.module._identity(changed_views[-1])
        self.assertEqual(windows_initial, (11, 22, 33188, 44, 55, 77))
        self.assertEqual(windows_changed_ctime, (11, 22, 33188, 44, 55, 77))
        self.assertEqual(windows_changed_ctime, windows_initial)
        for (field, unused_value, expected), actual in zip(changes, windows_changes):
            with self.subTest(identity_field=field):
                self.assertEqual(actual, expected)
                self.assertNotEqual(actual, windows_initial)
        self.assertEqual(posix_initial, (11, 22, 33188, 44, 55, 66))
        self.assertEqual(posix_changed_ctime, (11, 22, 33188, 44, 55, 99))
        self.assertNotEqual(posix_changed_ctime, posix_initial)
        self.assertEqual(posix_changed_birthtime, (11, 22, 33188, 44, 55, 66))
        self.assertEqual(windows_fallback, (11, 22, 33188, 44, 55, 66))
        self.assertEqual(windows_fallback_changed, (11, 22, 33188, 44, 55, 99))
        self.assertNotEqual(windows_fallback_changed, windows_fallback)


if __name__ == "__main__":
    unittest.main()
