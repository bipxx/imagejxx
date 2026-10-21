"""Build a checked sdist from explicit source roots, without walking the checkout.

Run with the already installed scikit-build-core==0.11.6.  The backend keeps
ownership of metadata and tar formatting; only its unsafe discovery callback is
temporarily replaced.  This tool neither installs dependencies nor runs CMake.
"""

from __future__ import annotations

import argparse
import contextlib
import copy
from dataclasses import dataclass
import hashlib
import importlib
import json
import os
from pathlib import Path
import re
import stat
import tarfile
import tempfile
from typing import Any, Iterator

try:
    import tomllib
except ModuleNotFoundError:  # Python 3.10; already pinned by the build requirements.
    import tomli as tomllib


SOURCE_SUBTREES = (
    "include", "src", "tests", "examples", "scripts", "docs", "licenses",
    "cmake", ".github", "third_party",
)
ROOT_FILES = (
    ".gitignore", "CMakeLists.txt", "pyproject.toml", "README.md", "LICENSE",
    "THIRD_PARTY_NOTICES.md",
)
PRUNED_DIRECTORIES = frozenset({
    "build", "dist", "_skbuild", ".git", ".venv", ".cache", ".tools",
    ".pytest_cache", ".ruff_cache", "__pycache__", ".tox", ".nox",
    "__pypackages__", ".mypy_cache", ".tmp", "node_modules", "CMakeFiles",
})
PRUNED_FILES = frozenset({".DS_Store", "Thumbs.db", "imgui.ini", "CMakeCache.txt"})
PRUNED_SUFFIXES = frozenset({
    ".pyc", ".pyo", ".pyd", ".so", ".dylib", ".dll", ".obj", ".o",
    ".a", ".lib", ".exe", ".pdb", ".ilk",
})
DEFAULT_MAX_FILES = 50_000
DEFAULT_MAX_BYTES = 256 * 1024 * 1024
MAX_ARCHIVE_BYTES = 512 * 1024 * 1024
MAX_REPORT_BYTES = 16 * 1024 * 1024
BACKEND_VERSION = "0.11.6"
CHUNK_BYTES = 1024 * 1024


class SourceDistributionError(RuntimeError):
    """A refused input or an incomplete/mismatched source distribution."""


@dataclass(frozen=True)
class FrozenFile:
    relative_path: str
    size: int
    sha256: str
    stat_identity: tuple[int, ...]


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise SourceDistributionError(message)


def _absolute(path: Path | str) -> Path:
    # resolve() would follow a junction before it could be refused.
    return Path(os.path.abspath(os.fspath(path)))


def _identity(info: os.stat_result) -> tuple[int, ...]:
    # On Windows Python 3.12 lstat's ctime can still denote creation while
    # fstat's denotes change.  Birthtime agrees for the same open file.  POSIX
    # keeps ctime so metadata replacement remains part of its mutation check.
    stable_time = (
        getattr(info, "st_birthtime_ns", info.st_ctime_ns)
        if os.name == "nt" else info.st_ctime_ns
    )
    return (
        info.st_dev, info.st_ino, info.st_mode, info.st_size,
        info.st_mtime_ns, stable_time,
    )


def _checked_stat(path: Path) -> os.stat_result:
    info = os.lstat(path)
    _require(
        not stat.S_ISLNK(info.st_mode)
        and not getattr(info, "st_file_attributes", 0)
        & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400),
        f"Symlink/junction/reparse input refused: {path}",
    )
    return info


def _check_ancestors(path: Path) -> None:
    for current in (*reversed(path.parents), path):
        _checked_stat(current)


@contextlib.contextmanager
def _open_regular(path: Path) -> Iterator[Any]:
    _check_ancestors(path.parent)
    before = _checked_stat(path)
    _require(stat.S_ISREG(before.st_mode), f"Not a regular file: {path}")
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags)
    with os.fdopen(descriptor, "rb") as stream:
        _require(_identity(os.fstat(stream.fileno())) == _identity(before),
                 f"Input changed while opening: {path}")
        yield stream
        _require(_identity(os.fstat(stream.fileno())) == _identity(before),
                 f"Input changed while reading: {path}")
    _require(_identity(_checked_stat(path)) == _identity(before),
             f"Input changed while reading: {path}")


def _hash_regular(path: Path, max_bytes: int) -> tuple[int, str, tuple[int, ...]]:
    info = _checked_stat(path)
    _require(info.st_size <= max_bytes, f"Source byte limit exceeded: {path}")
    digest = hashlib.sha256()
    size = 0
    with _open_regular(path) as stream:
        while block := stream.read(CHUNK_BYTES):
            size += len(block)
            _require(size <= max_bytes, f"Source byte limit exceeded: {path}")
            digest.update(block)
    _require(size == info.st_size and _identity(_checked_stat(path)) == _identity(info),
             f"Input changed while hashing: {path}")
    return size, digest.hexdigest(), _identity(info)


def _pruned_name(name: str) -> bool:
    # Name pruning precedes entry.stat or descent: inaccessible generated trees
    # and nested .gitignore discovery inside them are never attempted.
    folded = name.casefold()
    return (
        folded in {item.casefold() for item in PRUNED_DIRECTORIES}
        or folded in {item.casefold() for item in PRUNED_FILES}
        or folded.endswith((".egg-info", ".dist-info"))
        or Path(name).suffix.lower() in PRUNED_SUFFIXES
    )


def collect_sources(
    root: Path | str, *, max_files: int = DEFAULT_MAX_FILES,
    max_bytes: int = DEFAULT_MAX_BYTES,
) -> tuple[FrozenFile, ...]:
    """Freeze only the ten required subtrees and six explicitly named root files.

    Ignore files are source inputs, not discovery rules.  Neither the checkout
    root nor any excluded build/environment tree is enumerated.
    """
    _require(0 < max_files <= 100_000, "max_files must be between 1 and 100000")
    _require(0 < max_bytes <= 1024 * 1024 * 1024, "max_bytes must be between 1 and 1 GiB")
    root = _absolute(root)
    _check_ancestors(root)
    _require(stat.S_ISDIR(_checked_stat(root).st_mode), f"Not a source directory: {root}")
    frozen: list[FrozenFile] = []
    total = 0
    visited_entries = 0

    def add(path: Path) -> None:
        nonlocal total
        _require(len(frozen) < max_files, "Source file limit exceeded")
        size, digest, identity = _hash_regular(path, max_bytes - total)
        total += size
        frozen.append(FrozenFile(path.relative_to(root).as_posix(), size, digest, identity))

    def visit(directory: Path, depth: int = 0) -> None:
        nonlocal visited_entries
        _require(depth <= 64, "Source directory depth limit exceeded")
        before = _checked_stat(directory)
        _require(stat.S_ISDIR(before.st_mode), f"Not a source directory: {directory}")
        # Do not use os.walk/rglob: the allowlist is the traversal boundary.
        with os.scandir(directory) as entries:
            for entry in entries:
                visited_entries += 1
                _require(visited_entries <= max_files * 8 + len(SOURCE_SUBTREES),
                         "Source directory-entry limit exceeded")
                if _pruned_name(entry.name):
                    continue
                path = directory / entry.name
                info = _checked_stat(path)
                if stat.S_ISDIR(info.st_mode):
                    visit(path, depth + 1)
                elif stat.S_ISREG(info.st_mode):
                    add(path)
                else:
                    raise SourceDistributionError(f"Non-regular source input refused: {path}")
        _require(_identity(_checked_stat(directory)) == _identity(before),
                 f"Source directory changed during collection: {directory}")

    for name in ROOT_FILES:
        add(root / name)
    for name in SOURCE_SUBTREES:
        visit(root / name)
    return tuple(sorted(frozen, key=lambda member: member.relative_path))


def _verify_frozen(root: Path, frozen: tuple[FrozenFile, ...], **limits: int) -> None:
    _require(collect_sources(root, **limits) == frozen,
             "Source membership, identity or bytes changed after collection")


def _read_pyproject(root: Path, frozen: tuple[FrozenFile, ...]) -> dict[str, Any]:
    with _open_regular(root / "pyproject.toml") as stream:
        pyproject = tomllib.load(stream)
    build = pyproject.get("build-system", {})
    _require(build.get("build-backend") == "scikit_build_core.build",
             "Only the pinned scikit_build_core.build backend is supported")
    _require("scikit-build-core==0.11.6" in build.get("requires", []),
             "Build requirements must pin scikit-build-core==0.11.6")
    _require(not build.get("backend-path"), "Custom backend-path is unsafe")
    project = pyproject.get("project", {})
    settings = pyproject.get("tool", {}).get("scikit-build", {})
    _require(not project.get("dynamic"), "Dynamic project metadata is unsafe")
    for key in ("metadata", "generate", "overrides"):
        _require(not settings.get(key), f"scikit-build {key} hooks are unsafe")
    _require(not settings.get("sdist", {}).get("cmake", False),
             "CMake-on-sdist is unsafe")
    # SettingsReader can read CMakeLists.txt even without configuring CMake.
    # Keep that automatic version inspection on the explicitly frozen root file.
    _require(settings.get("cmake", {}).get("source-dir", ".") in (".", "./"),
             "External/alternate CMake source-dir inspection is unsafe")
    _require(not project.get("license-files"), "License glob discovery is unsafe")
    _require(not any(key.upper().startswith("SKBUILD_") for key in os.environ),
             "SKBUILD_* environment overrides are unsafe")
    names = {member.relative_path for member in frozen}
    for key in ("readme", "license"):
        value = project.get(key)
        reference = value if key == "readme" and isinstance(value, str) else (
            value.get("file") if isinstance(value, dict) else None
        )
        if reference is not None:
            _require(isinstance(reference, str) and reference in names,
                     f"project.{key} must reference a frozen source file")
    return pyproject


def _load_backend() -> Any:
    return importlib.import_module("scikit_build_core.build.sdist")


def _metadata(backend: Any, pyproject: dict[str, Any]) -> tuple[str, bytes]:
    if hasattr(backend, "expected_pkg_info"):  # Isolated tests, never a CLI option.
        name = re.sub(r"[-_.]+", "_", pyproject["project"]["name"]).lower()
        return f"{name}-{pyproject['project']['version']}", backend.expected_pkg_info(pyproject)
    reader = backend.SettingsReader(pyproject, {}, state="sdist")
    reader.validate_may_exit()
    settings = reader.settings
    _require(not settings.sdist.cmake and not settings.generate and not settings.metadata,
             "Resolved backend settings enable unsafe source hooks")
    metadata = backend.get_standard_metadata(pyproject, settings)
    name = (
        backend.canonicalize_name(metadata.name).replace("-", "_")
        if settings.minimum_version is None or settings.minimum_version >= backend.Version("0.5")
        else metadata.name
    )
    return f"{name}-{metadata.version}", bytes(copy.deepcopy(metadata).as_rfc822())


def _ensure_directory(path: Path) -> None:
    missing: list[Path] = []
    current = path
    while True:
        try:
            info = _checked_stat(current)
            _require(stat.S_ISDIR(info.st_mode), f"Not an output directory: {current}")
            _check_ancestors(current)
            break
        except FileNotFoundError:
            missing.append(current)
            current = current.parent
    for item in reversed(missing):
        item.mkdir()
        _require(stat.S_ISDIR(_checked_stat(item).st_mode), f"Unsafe output directory: {item}")


def _require_absent(path: Path) -> None:
    # Check existing parents first, without walking through a reparse point to
    # discover whether the final output already exists.
    for parent in reversed(path.parents):
        try:
            info = _checked_stat(parent)
        except FileNotFoundError:
            break
        _require(stat.S_ISDIR(info.st_mode), f"Not an output directory: {parent}")
    try:
        os.lstat(path)
    except FileNotFoundError:
        return
    raise SourceDistributionError(f"Refusing existing output: {path}")


def _outside_sources(root: Path, output: Path) -> None:
    for name in SOURCE_SUBTREES:
        _require(not output.is_relative_to(root / name),
                 f"Output must be outside source collection roots: {output}")
    _require(output not in {root / name for name in ROOT_FILES},
             f"Output aliases a required root file: {output}")


def _remove_owned_file(path: Path, identity: tuple[int, int]) -> None:
    _check_ancestors(path.parent)
    try:
        actual = _checked_stat(path)
    except FileNotFoundError:
        return
    _require((actual.st_dev, actual.st_ino) == identity,
             f"Output identity changed; refusing cleanup: {path}")
    path.unlink()


def _verify_archive(
    path: Path, root: Path, frozen: tuple[FrozenFile, ...], prefix: str, pkg_info: bytes,
) -> None:
    _require(_checked_stat(path).st_size <= MAX_ARCHIVE_BYTES, "Archive byte limit exceeded")
    expected = {f"{prefix}/{member.relative_path}": member for member in frozen}
    pkg_name = f"{prefix}/PKG-INFO"
    seen: set[str] = set()
    with tarfile.open(path, "r:gz") as archive:
        for member in archive:
            _require(member.name not in seen, f"Duplicate archive member: {member.name}")
            seen.add(member.name)
            _require(member.isfile() and not member.issym() and not member.islnk(),
                     f"Non-file archive member: {member.name}")
            payload = archive.extractfile(member)
            _require(payload is not None, f"Unreadable archive member: {member.name}")
            with payload:
                if member.name == pkg_name:
                    _require(member.size == len(pkg_info) and payload.read() == pkg_info,
                             "Generated PKG-INFO bytes differ from pinned backend metadata")
                    continue
                _require(member.name in expected, f"Unexpected archive member: {member.name}")
                source = expected[member.name]
                _require(member.size == source.size, f"Archive size mismatch: {member.name}")
                digest = hashlib.sha256()
                with _open_regular(root / source.relative_path) as original:
                    while block := payload.read(CHUNK_BYTES):
                        _require(block == original.read(len(block)),
                                 f"Archive bytes mismatch: {member.name}")
                        digest.update(block)
                    _require(not original.read(1), f"Archive truncated source: {member.name}")
                _require(digest.hexdigest() == source.sha256,
                         f"Archive differs from frozen source: {member.name}")
    _require(seen == set(expected) | {pkg_name}, "Archive source/PKG-INFO membership mismatch")


def build_source_distribution(
    root: Path | str, output: Path | str, report: Path | str, *,
    max_files: int = DEFAULT_MAX_FILES, max_bytes: int = DEFAULT_MAX_BYTES,
    _backend: Any = None,
) -> dict[str, Any]:
    """Publish an exclusively created archive/report only after exact validation."""
    root, output, report = map(_absolute, (root, output, report))
    _outside_sources(root, output)
    _outside_sources(root, report)
    _require_absent(report)
    limits = {"max_files": max_files, "max_bytes": max_bytes}
    frozen = collect_sources(root, **limits)
    pyproject = _read_pyproject(root, frozen)
    backend = _load_backend() if _backend is None else _backend
    _require(backend.__version__ == BACKEND_VERSION,
             f"Installed scikit-build-core must be exactly {BACKEND_VERSION}")
    source_pins = []
    if _backend is None:
        for path in (Path(backend.__file__), Path(backend.__file__).with_name("_file_processor.py")):
            size, digest, _ = _hash_regular(_absolute(path), DEFAULT_MAX_BYTES)
            source_pins.append({"path": str(_absolute(path)), "size": size, "sha256": digest})
    original_collector = backend.each_unignored_file
    original_cwd = os.getcwd()
    stage: Path | None = None
    stage_archive: Path | None = None
    published_archive: Path | None = None
    archive_identity: tuple[int, int] | None = None
    report_identity: tuple[int, int] | None = None
    succeeded = False
    try:
        backend.each_unignored_file = lambda *args, **kwargs: (
            Path(member.relative_path) for member in frozen
        )
        os.chdir(root)
        prefix, pkg_info = _metadata(backend, pyproject)
        _require(isinstance(pkg_info, bytes) and len(pkg_info) <= max_bytes,
                 "Generated PKG-INFO exceeds the byte limit")
        _require(bool(re.fullmatch(r"[A-Za-z0-9_.+!-]+", prefix)), "Unsafe archive name")
        filename = f"{prefix}.tar.gz"
        final_archive = output / filename
        _require(final_archive != report, "Archive and report paths must differ")
        _require_absent(final_archive)
        _ensure_directory(output)
        _verify_frozen(root, frozen, **limits)
        stage = Path(tempfile.mkdtemp(prefix=".source-distribution-", dir=output))
        stage_archive = stage / filename
        returned = backend.build_sdist(str(stage))
        _require(returned == filename, f"Unexpected backend archive name: {returned}")
        _verify_frozen(root, frozen, **limits)
        _verify_archive(stage_archive, root, frozen, prefix, pkg_info)
        _verify_frozen(root, frozen, **limits)
        size, digest, _ = _hash_regular(stage_archive, MAX_ARCHIVE_BYTES)
        record = {
            "schema_version": 1, "success": True,
            "archive": {"path": str(final_archive), "size": size, "sha256": digest},
            "backend": {"version": BACKEND_VERSION, "source_pins": source_pins},
            "collection": {"subtrees": list(SOURCE_SUBTREES), "root_files": list(ROOT_FILES),
                           "max_files": max_files, "max_bytes": max_bytes,
                           "ignored_file_rules": "explicit positive roots; generated names pruned before descent"},
            "source_files": [{"path": member.relative_path, "size": member.size,
                              "sha256": member.sha256} for member in frozen],
            "pkg_info": {"path": f"{prefix}/PKG-INFO", "size": len(pkg_info),
                         "sha256": hashlib.sha256(pkg_info).hexdigest()},
            "verification": "exact member membership and byte equality; PKG-INFO independently generated",
        }
        receipt = (json.dumps(record, indent=2, sort_keys=True) + "\n").encode("utf-8")
        _require(len(receipt) <= MAX_REPORT_BYTES, "Report byte limit exceeded")
        _check_ancestors(output)
        with final_archive.open("xb") as destination:
            published_archive = final_archive
            created = os.fstat(destination.fileno())
            archive_identity = created.st_dev, created.st_ino
            copied = hashlib.sha256()
            with _open_regular(stage_archive) as source:
                while block := source.read(CHUNK_BYTES):
                    destination.write(block)
                    copied.update(block)
            _require(copied.hexdigest() == digest, "Archive changed during exclusive publication")
        # Removing only these exact, privately created paths also refuses an
        # unexpected backend output without recursively inspecting its tree.
        stage_archive.unlink()
        stage_archive = None
        stage.rmdir()
        stage = None
        _ensure_directory(report.parent)
        with report.open("xb") as destination:
            created = os.fstat(destination.fileno())
            report_identity = created.st_dev, created.st_ino
            destination.write(receipt)
        _verify_frozen(root, frozen, **limits)
        succeeded = True
        return record
    finally:
        backend.each_unignored_file = original_collector
        try:
            os.chdir(original_cwd)
        except BaseException:
            succeeded = False
            raise
        finally:
            if not succeeded:
                if report_identity is not None:
                    _remove_owned_file(report, report_identity)
                if published_archive is not None and archive_identity is not None:
                    _remove_owned_file(published_archive, archive_identity)
            # Exact owned paths only; never a recursive delete or directory walk.
            if stage_archive is not None:
                with contextlib.suppress(FileNotFoundError):
                    stage_archive.unlink()
            if stage is not None:
                # Preserve unknown backend material on failure; never walk or
                # recursively erase it, and do not mask the original failure.
                with contextlib.suppress(OSError):
                    stage.rmdir()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="Directory receiving the archive")
    parser.add_argument("--report", type=Path, required=True, help="Fresh success receipt JSON")
    parser.add_argument("--max-files", type=int, default=DEFAULT_MAX_FILES)
    parser.add_argument("--max-bytes", type=int, default=DEFAULT_MAX_BYTES)
    args = parser.parse_args()
    try:
        record = build_source_distribution(args.root, args.output, args.report,
                                           max_files=args.max_files, max_bytes=args.max_bytes)
    except (SourceDistributionError, OSError, ValueError, tarfile.TarError, ImportError) as error:
        parser.exit(1, f"Source distribution refused: {error}\n")
    print(f"Verified {len(record['source_files'])} source files: {record['archive']['path']}")


if __name__ == "__main__":
    main()
