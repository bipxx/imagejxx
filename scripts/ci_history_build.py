"""Bind a real hosted pip build to the installed-history source contract.

This wrapper owns one Windows/Linux pip command, not a test or parity result.
It retains a before-command source freeze, the terminal process proof, and the
three actual cache outputs needed by the existing history capture protocol.
No cache, dependency SDK, environment, or installed binding is copied here.

Darwin is deliberately refused before output creation. Ninja creates process
groups that the existing cooperative Mac broker does not register. Admitting
that build needs a Mac-native control proving cleanup of such descendants on
normal exit, failure, timeout and owner death, with an unrelated process intact.
Calling the current broker around pip would not establish that property.
"""
from __future__ import annotations

import argparse
import base64
import csv
from email.parser import BytesParser
import hashlib
import importlib.metadata
import importlib.util
import io
import json
import math
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import sys
import sysconfig
import time
from urllib.parse import urlsplit
from urllib.request import url2pathname


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
MAX_SOURCE_ROWS = 8192
MAX_METADATA_BYTES = 2 * 1024 * 1024
MAX_INPUT_BYTES = 256 * 1024 * 1024
MAX_LOG_BYTES = 128 * 1024 * 1024
MAX_CACHE_DIRECTORIES = 16384
SOURCE_PREFIXES = ("src/", "include/", "examples/", "third_party/", "cmake/", "scripts/",
                   "licenses/", "tests/native/", ".github/workflows/")
REQUIRED_SOURCES = frozenset((
    "CMakeLists.txt", "pyproject.toml", "README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", ".gitattributes",
    "scripts/ci_history_build.py", "scripts/ci_python_suite.py",
    ".github/workflows/test.yml", ".github/workflows/source-platform.yml",
    "src/desktop/command_metadata.hpp",
))
CG0_EXCLUDED = frozenset((
    "docs/packets/CG0-analytic-reference-contract.md",
    "docs/packets/CG0-imagej-observer-contract.md",
    "tests/reference/continuous_bezier_exact_reference.py",
    "tests/reference/ObserveContinuousPathBooleans.java",
))
GENERATED_ROOTS = frozenset(("build", "dist", ".tmp", ".cache", ".venv", ".tools", ".git"))


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _checked_path(value):
    """Absolute spelling with no linked/reparse ancestor, including outputs."""
    path = Path(os.path.abspath(value))
    for member in reversed((path, *path.parents)):
        if not os.path.lexists(member):
            continue
        info = member.lstat()
        _require(not stat.S_ISLNK(info.st_mode) and
                 not getattr(info, "st_file_attributes", 0) & 0x400,
                 "Linked/reparse path is not admitted: " + str(member))
    return path


def _pin(value, *, allow_empty=False, maximum=MAX_INPUT_BYTES):
    path = _checked_path(value)
    before = path.lstat()
    _require(stat.S_ISREG(before.st_mode) and
             (0 if allow_empty else 1) <= before.st_size <= maximum,
             "Input is not a bounded regular file: " + str(path))
    digest, size = hashlib.sha256(), 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            size += len(block)
            _require(size <= before.st_size, "Input grew while being pinned")
            digest.update(block)
        after = os.fstat(stream.fileno())
    final = path.lstat()
    identity = lambda row: (row.st_dev, row.st_ino, row.st_size, row.st_mtime_ns)
    _require(size == before.st_size and identity(before) == identity(after) == identity(final),
             "Input changed while being pinned: " + str(path))
    # The capture pin uses resolve() too. On Windows the installed RECORD may
    # spell lib/Lib differently even though the file identity is unchanged.
    canonical = _checked_path(path.resolve(strict=True))
    _require(identity(canonical.lstat()) == identity(final),
             "Canonical input changed after being pinned: " + str(path))
    return {"path": str(canonical), "bytes": size, "sha256": digest.hexdigest()}


def _json(path):
    before = _pin(path, maximum=MAX_METADATA_BYTES)
    def unique(pairs):
        result = {}
        for key, value in pairs:
            _require(key not in result, "Duplicate metadata JSON key: " + key)
            result[key] = value
        return result
    def invalid(value):
        raise ValueError("Nonfinite metadata JSON value: " + value)
    value = json.loads(Path(path).read_bytes(), object_pairs_hook=unique, parse_constant=invalid)
    _require(_pin(path, maximum=MAX_METADATA_BYTES) == before, "Metadata changed during read")
    return value


def _save(path, value, *, exclusive=False):
    raw = (json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n").encode("utf-8")
    _require(len(raw) <= MAX_METADATA_BYTES, "Build metadata exceeds 2 MiB")
    temporary = path if exclusive else path.with_name(path.name + ".writing")
    with temporary.open("xb") as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    if not exclusive:
        os.replace(temporary, path)


def _load_script(root, name):
    path = root / "scripts" / (name + ".py")
    before = _pin(path)
    source = path.read_bytes()
    _require(len(source) == before["bytes"] and hashlib.sha256(source).hexdigest() == before["sha256"],
             "Source helper changed while reading")
    spec = importlib.util.spec_from_file_location("imagejxx_history_build_" + name, path)
    module = importlib.util.module_from_spec(spec)
    # -B forbids cache writes, not reads. Execute exactly the pinned source bytes.
    exec(compile(source, spec.origin, "exec", dont_inherit=True), module.__dict__)
    _require(_pin(path) == before, "Source helper changed while loading")
    return module


def _tracked_paths(root):
    # A bounded, read-only Git metadata query. No product or installed module is
    # imported, and repository fsmonitor hooks are not invoked.
    result = subprocess.run(["git", "-c", "core.fsmonitor=false", "ls-files", "-z"],
                            cwd=root, capture_output=True, timeout=30, check=True)
    _require(len(result.stdout) <= MAX_METADATA_BYTES, "Tracked path listing exceeds bound")
    return {name.decode("utf-8") for name in result.stdout.split(b"\0") if name}


def _history_paths(root):
    capture = _load_script(root, "capture_shortcuts_history_ui")
    # The same authenticated native build feeds the four paired UI packets. Reader
    # sources are outside SOURCE_PREFIXES, so admit their explicit role union.
    return set().union(*(set((capture.COMMON | capture.SPEC[kind]["roles"]).values())
                         for kind in ("history", "appearance", "drawing", "playback")))


def _logical_name(name):
    _require(isinstance(name, str) and name and "\\" not in name and "\0" not in name,
             "Invalid logical source path")
    path = PurePosixPath(name)
    _require(not path.is_absolute() and path.as_posix() == name and
             all(part not in ("", ".", "..") for part in name.split("/")),
             "Noncanonical logical source path: " + name)
    return path


def _source_paths(root):
    tracked = {_logical_name(name).as_posix() for name in _tracked_paths(root)}
    roles = {_logical_name(name).as_posix() for name in _history_paths(root)}
    selected = {name for name in tracked if name.startswith(SOURCE_PREFIXES)
                or name == "conftest.py" or name.endswith("/conftest.py")}
    selected |= REQUIRED_SOURCES | roles
    selected = {name for name in selected if name not in CG0_EXCLUDED and
                PurePosixPath(name).parts[0] not in GENERATED_ROOTS and
                "__pycache__" not in PurePosixPath(name).parts}
    _require(roles <= selected and REQUIRED_SOURCES <= selected,
             "Required source role would be excluded")
    _require(0 < len(selected) <= MAX_SOURCE_ROWS, "Source freeze exceeds 8192 rows")
    return sorted(selected), roles


def freeze_sources(root, output):
    """Write the exclusive schema-1 freeze before any owned build command."""
    root, output = _checked_path(root), _checked_path(output)
    _require(root.is_dir() and output.is_dir() and output.is_relative_to(root),
             "Source freeze requires existing repository-owned output")
    selected, roles = _source_paths(root)
    rows = [{"logical_path": name, **_pin(root / name, allow_empty=name not in roles)}
            for name in selected]
    path = output / "source-freeze.json"
    _save(path, {"schema_version": 1, "repository": str(root),
                 "scope": "tracked production/configuration/vendored/license closure and explicit history roles; CG0 excluded",
                 "sources": rows}, exclusive=True)
    return path


def verify_sources(freeze):
    """Verify frozen membership and bytes without updating any recorded pin."""
    document = _json(freeze)
    _require(document.get("schema_version") == 1 and isinstance(document.get("sources"), list),
             "Invalid source freeze schema")
    rows = document["sources"]
    _require(0 < len(rows) <= MAX_SOURCE_ROWS, "Invalid source freeze row count")
    root = _checked_path(document["repository"])
    expected, roles = _source_paths(root)
    _require([row["logical_path"] for row in rows] == expected,
             "Source membership changed since the before-command freeze")
    for row in rows:
        name = _logical_name(row["logical_path"]).as_posix()
        _require(set(row) == {"logical_path", "path", "bytes", "sha256"} and
                 row["path"] == str(root / name), "Source row/path differs from repository")
        actual = _pin(root / name, allow_empty=name not in roles)
        _require(actual == {key: row[key] for key in ("path", "bytes", "sha256")},
                 "Source changed since freeze: " + name)
    return document


def _load_owner(root):
    module = _load_script(root, "ci_owned_process")
    return module.run_owned, _pin(root / "scripts/ci_owned_process.py")


def _build_products(root):
    # Same strict wheel-cache discovery and complete Ninja/Release admission as
    # ci_scientific_sdk.wheel_cache and ci_python_suite.current_producer_cache.
    # Kept local to avoid a circular import when the suite consumes this receipt.
    wheel = _checked_path(root / "build/wheel")
    _require(wheel.is_dir(), "Existing wheel build cache is unavailable")
    found, visited = [], 0
    def fail(error):
        raise error
    for parent, directories, names in os.walk(wheel, followlinks=False, onerror=fail):
        visited += 1
        _require(visited <= MAX_CACHE_DIRECTORIES, "Wheel cache discovery exceeds bound")
        for name in directories:
            _require(_checked_path(Path(parent) / name).is_dir(), "Non-directory cache member")
        if "CMakeCache.txt" in names:
            found.append(_checked_path(Path(parent) / "CMakeCache.txt"))
    _require(len(found) == 1, "Expected exactly one build/wheel CMake cache")
    cache = found[0]
    before = _pin(cache, maximum=4 * 1024 * 1024)
    values = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if ":" not in key:
            continue
        key = key.split(":", 1)[0]
        _require(key not in values, "Duplicate cache field: " + key)
        values[key] = value
    configuration = {"CMAKE_GENERATOR": "Ninja", "CMAKE_BUILD_TYPE": "Release",
                     "BUILD_TESTING": "ON", "IMAGEJXX_BUILD_PYTHON": "ON",
                     "IMAGEJXX_BUILD_IO": "ON", "IMAGEJXX_BUILD_CPP_UI": "ON",
                     "IMAGEJXX_BUILD_PYTHON_UI": "ON"}
    _require(all(values.get(key) == value for key, value in configuration.items()) and
             not values.get("CMAKE_CONFIGURATION_TYPES"), "Cache is not the complete Ninja/Release profile")
    for key, expected in (("CMAKE_HOME_DIRECTORY", root), ("CMAKE_CACHEFILE_DIR", cache.parent)):
        _require(values.get(key) and Path(values[key]).is_absolute() and
                 _checked_path(values[key]) == expected, "Cache directory identity differs: " + key)
        configuration[key] = str(expected)
    configuration["IMAGEJXX_CI_SOURCE_PROFILE"] = values.get("IMAGEJXX_CI_SOURCE_PROFILE", "")
    suffix = sysconfig.get_config_var("EXT_SUFFIX")
    _require(isinstance(suffix, str) and suffix and "/" not in suffix and "\\" not in suffix,
             "Current Python extension suffix is unavailable")
    names = {"cpp": "imagejxx.exe" if sys.platform == "win32" else "imagejxx",
             "_native": "_native" + suffix, "_desktop": "_desktop" + suffix}
    products = {key: _pin(cache.parent / name) for key, name in names.items()}
    _require(_pin(cache, maximum=4 * 1024 * 1024) == before,
             "Cache changed during product admission")
    return before, configuration, products


def _final_products(root, raw_cache_products):
    """Verify installed outputs of the caller's completed pip build/install.

    RECORD authenticates the final bytes; it does not prove a bytewise strip
    transformation from the separately retained raw cache identities. Calling
    this function alone is metadata verification, never proof that a build ran.
    """
    _require(set(raw_cache_products) == {"cpp", "_native", "_desktop"},
             "Expected exactly three raw cache products")
    for row in raw_cache_products.values():
        _require(_pin(row["path"]) == row, "Raw cache product changed before packaging admission")
    distribution = importlib.metadata.distribution("imagejxx")
    members = distribution.files
    _require(members is not None and 0 < len(members) <= MAX_SOURCE_ROWS,
             "Installed distribution requires a bounded original RECORD")
    record_members = [str(member) for member in members
                      if PurePosixPath(str(member)).name == "RECORD" and
                      PurePosixPath(str(member)).parent.name.endswith(".dist-info")]
    _require(len(record_members) == 1, "Expected one installed distribution RECORD")
    record_name = record_members[0]
    record_logical = _logical_name(record_name)
    _require(len(record_logical.parts) == 2 and record_logical.parent.name.lower().startswith("imagejxx-"),
             "Installed RECORD is outside the imagejxx distribution metadata directory")
    record_path = _checked_path(distribution.locate_file(record_name))
    record_pin = _pin(record_path, maximum=MAX_METADATA_BYTES)
    rows = {}
    for row in csv.reader(io.StringIO(record_path.read_text(encoding="utf-8"), newline=""), strict=True):
        _require(len(rows) < MAX_SOURCE_ROWS and len(row) == 3, "Invalid or oversized installed RECORD")
        name, encoded, size = row
        logical = PurePosixPath(name)
        _require(name and "\\" not in name and "\0" not in name and not logical.is_absolute() and
                 logical.as_posix() == name and all(part not in ("", ".") for part in name.split("/")) and
                 ":" not in name and name not in rows, "Invalid or duplicate installed RECORD path")
        rows[name] = (encoded, size)
    _require(rows.get(record_name) == ("", ""), "Original RECORD must contain its unhashed self row")

    def recorded(name, *, maximum=MAX_INPUT_BYTES):
        _require(name in rows, "Installed file has no original RECORD row: " + name)
        encoded, size = rows[name]
        _require(0 < len(size) <= len(str(maximum)) and size.isascii() and size.isdecimal() and
                 size == str(int(size)),
                 "Installed RECORD size is not a canonical nonnegative integer")
        actual = _pin(distribution.locate_file(name), maximum=maximum)
        digest = base64.urlsafe_b64encode(bytes.fromhex(actual["sha256"])).rstrip(b"=").decode("ascii")
        _require(encoded == "sha256=" + digest and int(size) == actual["bytes"],
                 "Installed file differs from its original RECORD hash/size: " + name)
        return actual

    metadata_name = str(PurePosixPath(record_name).parent / "METADATA")
    direct_name = str(PurePosixPath(record_name).parent / "direct_url.json")
    metadata_pin = recorded(metadata_name, maximum=MAX_METADATA_BYTES)
    direct_pin = recorded(direct_name, maximum=MAX_METADATA_BYTES)
    metadata = BytesParser().parsebytes(Path(metadata_pin["path"]).read_bytes())
    names, versions = metadata.get_all("Name", []), metadata.get_all("Version", [])
    _require(len(names) == 1 and names[0].lower() == "imagejxx" and
             len(versions) == 1 and versions[0].strip() == versions[0] and versions[0],
             "Installed distribution metadata does not identify imagejxx")
    direct = _json(direct_pin["path"])
    _require(isinstance(direct, dict) and set(direct) == {"url", "dir_info"} and
             isinstance(direct["url"], str) and isinstance(direct["dir_info"], dict) and
             set(direct["dir_info"]) <= {"editable"} and
             direct["dir_info"].get("editable", False) is False,
             "Installed distribution must be the noneditable local-directory build")
    url = urlsplit(direct["url"])
    local = Path(url2pathname(url.path))
    _require(url.scheme == "file" and not url.netloc and not url.query and not url.fragment and
             local.is_absolute() and _checked_path(local) == root,
             "Installed distribution direct URL differs from this local source root")
    suffix = sysconfig.get_config_var("EXT_SUFFIX")
    _require(isinstance(suffix, str) and suffix and "/" not in suffix and "\\" not in suffix,
             "Current Python extension suffix is unavailable")
    application_name = "imagejxx.exe" if sys.platform == "win32" else "imagejxx"
    applications = [name for name in rows if PurePosixPath(name).name == application_name]
    _require(len(applications) == 1, "Expected one original RECORD application entry")
    selected = {"cpp": applications[0], "_native": "imagejxx/_native" + suffix,
                "_desktop": "imagejxx/_desktop" + suffix}
    products, product_rows = {}, {}
    raw_paths = {Path(row["path"]) for row in raw_cache_products.values()}
    for role, name in selected.items():
        actual = recorded(name)
        _require(Path(actual["path"]) not in raw_paths,
                 "Installed final output must be distinct from the raw build cache")
        products[role] = actual
        encoded, size = rows[name]
        product_rows[role] = {"record_path": name, "record_hash": encoded, "record_size": int(size)}
    for pin in (record_pin, metadata_pin, direct_pin):
        _require(_pin(pin["path"], maximum=MAX_METADATA_BYTES) == pin,
                 "Installed packaging metadata changed during admission")
    for pin in products.values():
        _require(_pin(pin["path"]) == pin, "Installed final output changed during admission")
    return products, {
        "phase": "pip-installed-final-outputs", "distribution_name": names[0],
        "distribution_version": versions[0], "source_url": direct["url"],
        "record": record_pin, "metadata": metadata_pin, "direct_url": direct_pin,
        "products": product_rows,
        "raw_to_final_relation": "authenticated build-and-install pipeline association; not a bytewise strip-derivation proof",
        "strip_policy": "backend install policy unchanged; transient wheel/staging bytes are not retained or reconstructed",
    }


def _command(argv):
    _require(isinstance(argv, (list, tuple)) and 5 <= len(argv) <= 1024 and
             all(isinstance(value, str) and "\0" not in value for value in argv) and
             sum(len(value.encode("utf-8")) + 1 for value in argv) <= 64 * 1024,
             "Expected bounded pip build arguments")
    _require(list(argv[1:5]) == ["-m", "pip", "install", ".[io,test]"],
             "Only the existing local .[io,test] pip install build is admitted")
    keys, index = set(), 5
    while index < len(argv):
        token = argv[index]
        if token == "-C":
            index += 1
            _require(index < len(argv), "Pip configuration argument is missing its value")
            setting = argv[index]
        else:
            _require(token.startswith("-C") and len(token) > 2,
                     "Only existing pip -C configuration arguments are admitted after .[io,test]")
            setting = token[2:]
        key, separator, value = setting.partition("=")
        _require(separator and key and value and key[0].isascii() and key[0].isalpha() and
                 all(character.isascii() and (character.isalnum() or character in "._-")
                     for character in key), "Malformed pip configuration key=value")
        variable = key.removeprefix("cmake.define.")
        definition = key.startswith("cmake.define.") and variable and \
            (variable[0].isascii() and (variable[0].isalpha() or variable[0] == "_")) and \
            all(character.isascii() and (character.isalnum() or character == "_")
                for character in variable)
        _require(definition or key in {"cmake.version", "cmake.args", "cmake.build-type", "ninja.version"},
                 "Pip configuration is outside the existing build recipe: " + key)
        _require(key != "cmake.args" or value == "-GNinja",
                 "Only the existing explicit Ninja generator argument is admitted")
        _require(key not in keys, "Duplicate pip configuration key: " + key)
        keys.add(key)
        index += 1
    selected = shutil.which(argv[0])
    _require(selected and Path(selected).resolve(strict=True).samefile(Path(sys.executable).resolve(strict=True)),
             "Pip must use this wrapper's actual Python interpreter")
    executable = _pin(Path(selected).resolve(strict=True))
    admitted = list(argv)
    # Launch exactly the interpreter whose identity was admitted above.
    # Windows CreateProcessW with lpApplicationName does not resolve a bare
    # Python alias through PATH as shutil.which does.
    admitted[0] = executable["path"]
    return admitted, executable


def verify_receipt(path, root):
    """Readmit one immutable completed build with the producer's own checks.

    This verifies the original pipeline association and current artifact bytes.
    It neither executes a command nor turns inventory metadata into new build
    evidence. In particular, final RECORD bytes remain distinct from raw
    linker outputs and do not certify a reconstructed stripping operation.
    """
    root = _checked_path(root)
    _require(root.is_dir(), "Build receipt requires an existing source root")
    before = _pin(path, maximum=MAX_METADATA_BYTES)
    document = _json(before["path"])
    _require(isinstance(document, dict) and type(document.get("schema_version")) is int and
             document["schema_version"] == 1 and
             document.get("kind") == "imagejxx.hosted-history-build.001" and
             document.get("state") == "passed" and document.get("acceptance") is False and
             document.get("repository") == str(root),
             "Expected this source root's completed hosted history build")

    def original(value, *, allow_empty=False, maximum=MAX_INPUT_BYTES):
        _require(isinstance(value, dict) and set(value) == {"path", "bytes", "sha256"} and
                 type(value["bytes"]) is int and isinstance(value["path"], str) and
                 isinstance(value["sha256"], str), "Invalid original build artifact pin")
        actual = _pin(value["path"], allow_empty=allow_empty, maximum=maximum)
        _require(actual == value, "Original build artifact changed: " + value["path"])
        return actual

    argv, executable = _command(document["argv"])
    _require(original(document["launch_executable"]) == executable,
             "Build launch interpreter differs from the admitted command")
    helper = original(document["owner_helper"])
    _require(helper == _pin(root / "scripts/ci_owned_process.py"),
             "Build owner differs from its source-root helper")
    process_pin = original(document["process"], maximum=MAX_METADATA_BYTES)
    proof = _json(process_pin["path"])
    _require(isinstance(proof, dict) and proof == document["command_proof"] and
             proof.get("schema") == "imagejxx.owned-command.001" and proof.get("state") == "exited" and
             type(proof.get("exit_code")) is int and proof["exit_code"] == 0 and
             proof.get("target_completed") is True and proof.get("cleanup_complete") is True and
             proof.get("timed_out") is False and proof.get("argv") == argv and
             proof.get("cwd") == str(root) and proof.get("helper") == helper,
             "Build process lacks its original successful owned terminal proof")
    freeze_pin = original(document["source_freeze"], maximum=MAX_METADATA_BYTES)
    _require(_json(freeze_pin["path"]).get("repository") == str(root),
             "Build source freeze belongs to another repository")
    freeze = verify_sources(freeze_pin["path"])
    _require(freeze["repository"] == str(root), "Build source freeze belongs to another repository")
    cache, configuration, raw = _build_products(root)
    _require(cache == document["cache"] and configuration == document["configuration"] and
             raw == document["raw_cache_products"],
             "Build cache configuration or raw products differ from their original admission")
    for name in ("record", "metadata", "direct_url"):
        original(document["packaging"][name], maximum=MAX_METADATA_BYTES)
    products, packaging = _final_products(root, raw)
    _require(products == document["built_products"] and packaging == document["packaging"],
             "Build final products or original RECORD association differ from their admission")
    original(document["log"], allow_empty=True, maximum=MAX_LOG_BYTES)
    original(process_pin, maximum=MAX_METADATA_BYTES)
    original(freeze_pin, maximum=MAX_METADATA_BYTES)
    original(helper)
    _require(_pin(before["path"], maximum=MAX_METADATA_BYTES) == before,
             "Build receipt changed during verification")
    return document


def run_build(root, output, argv, timeout=3600):
    """Run one real pip build; failures retain their original logs and receipt."""
    if sys.platform not in ("win32", "linux"):
        raise NotImplementedError("Darwin Ninja process-group ownership is not qualified; no build started")
    _require(type(timeout) in (int, float) and math.isfinite(timeout) and 0 < timeout <= 3600,
             "Build timeout must be finite and at most 3600 seconds")
    root, output = _checked_path(root), _checked_path(output)
    _require(root.is_dir() and output.is_relative_to(root) and output != root,
             "Use fresh repository-owned build output")
    relative = output.relative_to(root)
    _require(relative.parts[0] in ("build", ".tmp") and
             not output.is_relative_to(root / "build/wheel") and
             not (root / "build/wheel").is_relative_to(output), "Output overlaps production inputs or wheel cache")
    _require(not os.path.lexists(output), "Build output already exists")
    argv, executable = _command(argv)
    output.mkdir(parents=True, exist_ok=False)
    receipt_path, process_path, log_path = output / "receipt.json", output / "process.json", output / "build.log"
    record = {"schema_version": 1, "kind": "imagejxx.hosted-history-build.001",
              "state": "preparing", "repository": str(root), "argv": argv,
              "launch_executable": executable, "timeout_seconds": timeout,
              "source_times": {}, "acceptance": False,
              "scope": "actual Windows/Linux pip build and three outputs; no tests or full-platform acceptance",
              "log_policy": {"accepted_bytes": MAX_LOG_BYTES,
                             "enforcement": "checked after owned terminal cleanup; not a live byte bound"}}
    _save(receipt_path, record, exclusive=True)
    try:
        freeze = freeze_sources(root, output)
        record["source_freeze"] = _pin(freeze, maximum=MAX_METADATA_BYTES)
        record["source_times"]["frozen"] = time.time()
        verify_sources(freeze)
        owner, helper = _load_owner(root)
        record.update(state="running", owner_helper=helper)
        _save(receipt_path, record)
        with log_path.open("xb") as stream:
            proof = owner(argv, cwd=root, env=os.environ.copy(), timeout=timeout,
                          stdout=stream, stderr=subprocess.STDOUT, receipt_path=process_path)
        recorded_proof = _json(process_path)
        record.update(process=_pin(process_path, maximum=MAX_METADATA_BYTES), command_proof=recorded_proof)
        _require(proof == recorded_proof and proof.get("schema") == "imagejxx.owned-command.001" and
                 proof.get("state") == "exited" and
                 proof.get("exit_code") == 0 and proof.get("target_completed") is True and
                 proof.get("cleanup_complete") is True and proof.get("timed_out") is False and
                 proof.get("argv") == argv and proof.get("cwd") == str(root) and proof.get("helper") == helper,
                 "Pip build lacks a matching successful owned terminal proof")
        record["log"] = _pin(log_path, allow_empty=True, maximum=MAX_LOG_BYTES)
        _require(_pin(freeze, maximum=MAX_METADATA_BYTES) == record["source_freeze"],
                 "Before-command source freeze was modified")
        verify_sources(freeze)
        record["source_times"]["verified_after_command"] = time.time()
        cache, configuration, raw_cache_products = _build_products(root)
        products, packaging = _final_products(root, raw_cache_products)
        verify_sources(freeze)
        _require(_pin(freeze, maximum=MAX_METADATA_BYTES) == record["source_freeze"] and
                 _pin(helper["path"]) == helper, "Source/owner identity changed before completion")
        record["source_times"]["verified_final"] = time.time()
        record.update(state="passed", cache=cache, configuration=configuration,
                      raw_cache_products=raw_cache_products, built_products=products, packaging=packaging)
        _save(receipt_path, record)
        return record
    except BaseException as error:
        record.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        # Original owner receipts and logs remain authoritative, including
        # launch/timeout/cleanup failures. Never substitute a synthetic success.
        if process_path.is_file():
            try:
                record["command_proof"] = _json(process_path)
                record["process"] = _pin(process_path, maximum=MAX_METADATA_BYTES)
            except (OSError, ValueError) as evidence_error:
                record["process_retention_error"] = str(evidence_error)
        if log_path.is_file():
            try:
                record["log"] = _pin(log_path, allow_empty=True, maximum=MAX_LOG_BYTES)
            except (OSError, ValueError) as evidence_error:
                record["log_retained_unpinned"] = {"path": str(log_path), "bytes": log_path.stat().st_size,
                                                   "reason": str(evidence_error)}
        _save(receipt_path, record)
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=3600)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    root = args.root.absolute()
    output = args.output if args.output.is_absolute() else root / args.output
    receipt = run_build(root, output, command, args.timeout)
    print(json.dumps({"state": receipt["state"], "receipt": str(output / "receipt.json")}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
