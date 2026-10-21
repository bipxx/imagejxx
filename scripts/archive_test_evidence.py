"""Bounded lossless retention of existing evidence; never test acceptance.

Use explicit workspace-relative paths. Pack never deletes its source. Verify
streams every member. Restore writes one selected subtree into a fresh directory.
No product, reader, build, runtime receipt or scientific observation is changed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat
import struct
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "imagejxx.test-evidence-retention.001"
LIMITS = {"max_files": 8192, "max_entries": 16384,
          "max_source_bytes": 1536 * 1024**2, "max_archive_bytes": 192 * 1024**2,
          "max_member_bytes": 128 * 1024**2, "max_restore_bytes": 256 * 1024**2,
          "max_seconds": 180}
CHUNK = 1024 * 1024
MAX_JSON = 16 * 1024**2
MAX_NAME = 4096
STAT_KEYS = {"device", "inode", "mtime_ns", "ctime_ns", "links"}
DEVICE_NAMES = {"con", "prn", "aux", "nul", "clock$"} | {
    prefix + str(i) for prefix in ("com", "lpt") for i in range(1, 10)}


def _require(condition, message):
    if not condition:
        raise ValueError(message)


class Deadline:
    def __init__(self):
        self.start = time.monotonic()

    def check(self):
        if time.monotonic() - self.start > LIMITS["max_seconds"]:
            raise TimeoutError("Retention exceeded its 180-second deadline")

    def elapsed(self):
        return round(time.monotonic() - self.start, 6)


def _relative(value, *, dot=False):
    _require(isinstance(value, str), "Path must be a string")
    text = value.replace("\\", "/")
    if dot and text == ".":
        return "."
    _require(text and not text.startswith("/") and ":" not in text,
             "Use an explicit workspace-relative path")
    parts = text.split("/")
    for part in parts:
        _require(part not in {"", ".", ".."} and not part.endswith((" ", ".")),
                 "Unsafe or ambiguous path component")
        _require(not any(ord(ch) < 32 or ch in '<>\"|?*' for ch in part), "Unsafe path character")
        _require(part.split(".")[0].casefold() not in DEVICE_NAMES, "Windows device path refused")
    _require(len(text.encode("utf-8")) <= MAX_NAME, "Evidence path too long")
    return PurePosixPath(*parts).as_posix()


def _record_stat(value):
    return {"device": value.st_dev, "inode": value.st_ino, "mtime_ns": value.st_mtime_ns,
            "ctime_ns": value.st_ctime_ns, "links": value.st_nlink}


def _common_identity(value):
    # Windows lstat/fstat can expose different stable ctime values. Match the
    # common identity here, then retain ctime within each API view separately.
    return (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_nlink)


def _plain(path, *, directory=None):
    value = path.lstat()
    _require(not stat.S_ISLNK(value.st_mode) and
             not (getattr(value, "st_file_attributes", 0) & 0x400),
             f"Links, junctions and reparse points refused: {path}")
    is_dir = stat.S_ISDIR(value.st_mode)
    _require(is_dir or stat.S_ISREG(value.st_mode), f"Not a regular file/directory: {path}")
    if directory is not None:
        _require(is_dir is directory, f"Unexpected path type: {path}")
    if not is_dir:
        _require(value.st_nlink == 1, f"Hard-linked file refused: {path}")
    return value


def _chain(path):
    _require(path == ROOT or ROOT in path.parents, "Path escapes workspace")
    current = ROOT
    _plain(current, directory=True)
    for part in path.relative_to(ROOT).parts:
        current = current / part
        try:
            _plain(current)
        except FileNotFoundError:
            break


def _path(value):
    path = ROOT.joinpath(*PurePosixPath(_relative(value)).parts)
    _chain(path)
    return path


def _rel(path):
    return path.relative_to(ROOT).as_posix()


def _inside(path, parent):
    return path == parent or parent in path.parents


def _parents(path):
    _chain(path.parent)
    path.parent.mkdir(parents=True, exist_ok=True)
    _chain(path.parent)


def _fresh(paths):
    _require(len({str(path).casefold() for path in paths}) == len(paths), "Output paths must differ")
    for path in paths:
        _chain(path)
        _require(not path.exists(), f"Refusing existing output: {_rel(path)}")


def _json_bytes(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=True, allow_nan=False) + "\n").encode("ascii")


def _write_json(path, value):
    raw = _json_bytes(value)
    _require(len(raw) <= MAX_JSON, "Retention JSON exceeds 16 MiB")
    _parents(path)
    with path.open("xb") as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())


def _pairs(values):
    result = {}
    for key, value in values:
        _require(key not in result, "Duplicate JSON key")
        result[key] = value
    return result


def _bad_constant(value):
    raise ValueError(f"Nonfinite JSON token: {value}")


def _opened(path, expected=None):
    _chain(path)
    before = _plain(path, directory=False)
    stream = path.open("rb")
    try:
        actual = os.fstat(stream.fileno())
        _require(_common_identity(actual) == _common_identity(before),
                 f"Source changed during open: {_rel(path)}")
        if expected is not None:
            _require(before.st_size == expected["bytes"] and
                     _record_stat(before) == expected["source_stat"],
                     f"Source identity changed: {_rel(path)}")
        return stream, (before, actual)
    except BaseException:
        stream.close()
        raise


def _finish_open(path, stream, before):
    path_before, handle_before = before
    actual = os.fstat(stream.fileno())
    _chain(path)
    after = _plain(path, directory=False)
    _require(_common_identity(actual) == _common_identity(after) and
             (_record_stat(actual), actual.st_size) == (_record_stat(handle_before), handle_before.st_size) and
             (_record_stat(after), after.st_size) == (_record_stat(path_before), path_before.st_size),
             f"Source changed during read: {_rel(path)}")


def _hash(path, deadline, maximum):
    stream, before = _opened(path)
    digest, count = hashlib.sha256(), 0
    try:
        _require(before[0].st_size <= maximum, f"File exceeds its bound: {_rel(path)}")
        while block := stream.read(CHUNK):
            deadline.check()
            count += len(block)
            _require(count <= maximum, "Input grew beyond its bound")
            digest.update(block)
        _finish_open(path, stream, before)
    finally:
        stream.close()
    _require(count == before[0].st_size, "Input length changed")
    return {"path": _rel(path), "bytes": count, "sha256": digest.hexdigest()}


def _load(path, deadline):
    pin = _hash(path, deadline, MAX_JSON)
    stream, before = _opened(path)
    try:
        raw = stream.read(MAX_JSON + 1)
        _finish_open(path, stream, before)
    finally:
        stream.close()
    deadline.check()
    _require(len(raw) == pin["bytes"] and hashlib.sha256(raw).hexdigest() == pin["sha256"],
             "JSON changed between hash and parse")
    return json.loads(raw, object_pairs_hook=_pairs, parse_constant=_bad_constant), pin


def _snapshot(source, deadline):
    _chain(source)
    root_stat = _plain(source, directory=True)
    files, directories, byte_count, seen, pending = [], [], 0, set(), [source]
    while pending:
        deadline.check()
        parent = pending.pop()
        _chain(parent)
        children = []
        with os.scandir(parent) as scan:
            for child in scan:
                deadline.check()
                _require(len(children) + len(seen) + 1 < LIMITS["max_entries"],
                         "Directory entry bound exceeded")
                children.append(child)
        for child in sorted(children, key=lambda item: item.name):
            deadline.check()
            path = parent / child.name
            member = _relative(path.relative_to(source).as_posix())
            _require(member.casefold() not in seen, "Duplicate/case-aliased source path")
            seen.add(member.casefold())
            _require(len(seen) + 1 <= LIMITS["max_entries"], "Source entry bound exceeded")
            _chain(path)
            value = _plain(path)
            row = {"member": member, "source_path": _rel(path), "source_stat": _record_stat(value)}
            if stat.S_ISDIR(value.st_mode):
                directories.append(row)
                pending.append(path)
            else:
                _require(len(files) < LIMITS["max_files"], "Source file bound exceeded")
                _require(value.st_size <= LIMITS["max_member_bytes"], "Source member bound exceeded")
                byte_count += value.st_size
                _require(byte_count <= LIMITS["max_source_bytes"], "Source byte bound exceeded")
                pin = _hash(path, deadline, LIMITS["max_member_bytes"])
                _require(_record_stat(_plain(path, directory=False)) == row["source_stat"],
                         "Source changed while inventory was built")
                row.update(bytes=pin["bytes"], sha256=pin["sha256"])
                files.append(row)
    _require(files, "Refusing empty source tree")
    return {"source": _rel(source), "source_stat": _record_stat(root_stat),
            "directories": sorted(directories, key=lambda row: row["member"]),
            "files": sorted(files, key=lambda row: row["member"]),
            "file_count": len(files), "entry_count": len(seen) + 1, "source_bytes": byte_count}


def _snapshot_digest(snapshot):
    return hashlib.sha256(_json_bytes(snapshot)).hexdigest()


def _artifact_pin(path, snapshot, deadline):
    value, pin = _load(path, deadline)
    _require(isinstance(value, dict) and isinstance(value.get("artifacts"), list) and
             isinstance(value.get("state"), str),
             "Artifact receipt must contain its original artifacts array")
    prefix, wanted = snapshot["source"] + "/", {}
    for row in value["artifacts"]:
        deadline.check()
        _require(isinstance(row, dict) and {"path", "bytes", "sha256"} <= set(row),
                 "Malformed artifact receipt entry")
        name = _relative(row["path"])
        if name.startswith(prefix):
            _require(name not in wanted, "Duplicate pinned source path")
            _require(type(row["bytes"]) is int and row["bytes"] >= 0 and _is_sha(row["sha256"]),
                     "Malformed original artifact file identity")
            wanted[name] = {key: row[key] for key in ("bytes", "sha256")}
    actual = {row["source_path"]: {key: row[key] for key in ("bytes", "sha256")}
              for row in snapshot["files"]}
    _require(wanted == actual, "Selected source differs from original artifact receipt pins")
    return {**pin, "original_state": value.get("state"),
            "selected_file_count": len(wanted), "selected_bytes": snapshot["source_bytes"]}


class _CappedWriter:
    def __init__(self, stream, deadline):
        self.stream, self.deadline = stream, deadline

    def write(self, data):
        self.deadline.check()
        _require(self.stream.tell() + len(data) <= LIMITS["max_archive_bytes"],
                 "ZIP would exceed its fixed 192 MiB bound")
        return self.stream.write(data)

    def tell(self):
        return self.stream.tell()

    def seek(self, offset, whence=0):
        return self.stream.seek(offset, whence)

    def flush(self):
        return self.stream.flush()

    def seekable(self):
        return True


def _zip_info(name, directory=False):
    info = zipfile.ZipInfo(name + ("/" if directory else ""), (1980, 1, 1, 0, 0, 0))
    info.create_system = 3
    info.external_attr = ((stat.S_IFDIR | 0o700) if directory else (stat.S_IFREG | 0o600)) << 16
    if directory:
        info.external_attr |= 0x10
    info.compress_type = zipfile.ZIP_STORED if directory else zipfile.ZIP_DEFLATED
    return info


def _pack_zip(source, archive, snapshot, deadline):
    _parents(archive)
    with archive.open("xb") as target:
        with zipfile.ZipFile(_CappedWriter(target, deadline), "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=6, allowZip64=False) as output:
            for row in snapshot["directories"]:
                deadline.check()
                output.writestr(_zip_info(row["member"], True), b"")
            for row in snapshot["files"]:
                path = source.joinpath(*PurePosixPath(row["member"]).parts)
                stream, before = _opened(path, row)
                digest, size = hashlib.sha256(), 0
                try:
                    with output.open(_zip_info(row["member"]), "w") as destination:
                        while block := stream.read(CHUNK):
                            deadline.check()
                            size += len(block)
                            _require(size <= row["bytes"], "Source grew while packing")
                            destination.write(block)
                            digest.update(block)
                    _finish_open(path, stream, before)
                finally:
                    stream.close()
                _require(size == row["bytes"] and digest.hexdigest() == row["sha256"],
                         "Source bytes changed while packing")
        target.flush()
        os.fsync(target.fileno())


def _central_preflight(stream, deadline):
    # Bound the directory before ZipFile allocates its member objects.
    stream.seek(0, 2)
    size = stream.tell()
    _require(22 <= size <= LIMITS["max_archive_bytes"], "ZIP size outside its bound")
    stream.seek(-22, 2)
    fields = struct.unpack("<4s4H2LH", stream.read(22))
    signature, disk, start_disk, disk_count, count, central_size, offset, comment = fields
    _require(signature == b"PK\x05\x06" and disk == start_disk == comment == 0 and
             disk_count == count and 0 < count <= LIMITS["max_entries"] and
             offset + central_size == size - 22,
             "Only bounded single-disk/comment-free/non-ZIP64 archives supported")
    stream.seek(offset)
    found = 0
    seen = set()
    while stream.tell() < offset + central_size:
        deadline.check()
        header = stream.read(46)
        _require(len(header) == 46 and header[:4] == b"PK\x01\x02", "Malformed ZIP directory")
        name_size, extra_size, comment_size = struct.unpack_from("<3H", header, 28)
        _require(0 < name_size <= MAX_NAME and extra_size == comment_size == 0,
                 "Unexpected ZIP directory metadata")
        raw_name = stream.read(name_size)
        _require(len(raw_name) == name_size, "Truncated ZIP member name")
        flags = struct.unpack_from("<H", header, 8)[0]
        name = raw_name.decode("utf-8" if flags & 0x800 else "cp437")
        stripped = name[:-1] if name.endswith("/") else name
        _require(_relative(stripped) == stripped and stripped.casefold() not in seen,
                 "Unsafe or case-aliased ZIP directory member")
        seen.add(stripped.casefold())
        _require(stream.tell() <= offset + central_size, "ZIP directory overrun")
        found += 1
        _require(found <= LIMITS["max_entries"], "ZIP entry bound exceeded")
    _require(found == count, "ZIP directory count mismatch")
    stream.seek(0)


def _is_sha(value):
    return isinstance(value, str) and len(value) == 64 and all(ch in "0123456789abcdef" for ch in value)


def _validate_stat(value):
    _require(isinstance(value, dict) and set(value) == STAT_KEYS and
             all(type(v) is int and v >= 0 for v in value.values()), "Malformed source stat")


def _manifest_snapshot(value):
    keys = {"schema", "scope", "acceptance_claim", "limits", "source_snapshot",
            "source_snapshot_sha256", "source_unchanged_before_after", "archive",
            "artifact_receipt", "tool"}
    _require(isinstance(value, dict) and set(value) == keys and value["schema"] == SCHEMA and
             value["scope"] == "lossless_retention_only" and value["acceptance_claim"] is False and
             value["limits"] == LIMITS and value["source_unchanged_before_after"] is True,
             "Unsupported retention manifest")
    snapshot = value["source_snapshot"]
    _require(isinstance(snapshot, dict) and set(snapshot) == {
        "source", "source_stat", "directories", "files", "file_count", "entry_count", "source_bytes"},
        "Malformed source snapshot")
    source = _relative(snapshot["source"])
    _require(source == snapshot["source"], "Noncanonical source path")
    _validate_stat(snapshot["source_stat"])
    _require(_snapshot_digest(snapshot) == value["source_snapshot_sha256"], "Snapshot digest mismatch")
    files, directories = snapshot["files"], snapshot["directories"]
    _require(isinstance(files, list) and isinstance(directories, list) and
             0 < len(files) <= LIMITS["max_files"] and
             len(files) + len(directories) + 1 <= LIMITS["max_entries"], "Manifest entry bound exceeded")
    names, total = {}, 0
    for is_dir, rows in ((True, directories), (False, files)):
        _require(all(isinstance(row, dict) and isinstance(row.get("member"), str) for row in rows),
                 "Malformed manifest member list")
        _require([row["member"] for row in rows] == sorted(row["member"] for row in rows),
                 "Manifest entries are not sorted")
        for row in rows:
            expected = {"member", "source_path", "source_stat"} | (set() if is_dir else {"bytes", "sha256"})
            _require(set(row) == expected, "Malformed manifest entry")
            member = _relative(row["member"])
            _require(member == row["member"] and row["source_path"] == source + "/" + member,
                     "Original source path mismatch")
            _require(member.casefold() not in names, "Duplicate/case-aliased manifest member")
            names[member.casefold()] = (member, is_dir)
            _validate_stat(row["source_stat"])
            if not is_dir:
                _require(row["source_stat"]["links"] == 1 and type(row["bytes"]) is int and
                         0 <= row["bytes"] <= LIMITS["max_member_bytes"] and _is_sha(row["sha256"]),
                         "Malformed manifest file identity")
                total += row["bytes"]
    for member, _ in names.values():
        parent = PurePosixPath(member).parent
        while str(parent) != ".":
            _require(names.get(parent.as_posix().casefold()) == (parent.as_posix(), True),
                     "Missing or aliased manifest parent")
            parent = parent.parent
    _require(type(snapshot["file_count"]) is int and snapshot["file_count"] == len(files) and
             type(snapshot["entry_count"]) is int and snapshot["entry_count"] == len(names) + 1 and
             type(snapshot["source_bytes"]) is int and snapshot["source_bytes"] == total and
             total <= LIMITS["max_source_bytes"], "Source totals outside bounds")
    for pin in (value["archive"], value["tool"]):
        _require(isinstance(pin, dict) and set(pin) == {"path", "bytes", "sha256"} and
                 _relative(pin["path"]) == pin["path"] and type(pin["bytes"]) is int and
                 pin["bytes"] >= 0 and _is_sha(pin["sha256"]), "Malformed manifest pin")
    _require(value["archive"]["bytes"] <= LIMITS["max_archive_bytes"] and
             value["tool"]["bytes"] <= MAX_JSON, "Manifest pin exceeds its bound")
    original = value["artifact_receipt"]
    _require(isinstance(original, dict) and set(original) == {
        "path", "bytes", "sha256", "original_state", "selected_file_count", "selected_bytes"} and
        _relative(original["path"]) == original["path"] and
        type(original["bytes"]) is int and 0 <= original["bytes"] <= MAX_JSON and
        _is_sha(original["sha256"]) and type(original["selected_file_count"]) is int and
        original["selected_file_count"] == len(files) and type(original["selected_bytes"]) is int and
        original["selected_bytes"] == total and isinstance(original["original_state"], str),
        "Malformed original artifact receipt binding")
    return snapshot


def _verify_payload(archive, snapshot, deadline):
    expected = {row["member"]: row for row in snapshot["files"]}
    dirs = {row["member"] + "/" for row in snapshot["directories"]}
    stream, before = _opened(archive)
    try:
        _central_preflight(stream, deadline)
        with zipfile.ZipFile(stream) as packed:
            entries = packed.infolist()
            _require(len(entries) == len(expected) + len(dirs), "ZIP member count mismatch")
            seen = set()
            for entry in entries:
                deadline.check()
                name = entry.filename
                _require(name not in seen, "Duplicate ZIP member")
                seen.add(name)
                is_dir = name.endswith("/")
                stripped = name[:-1] if is_dir else name
                _require(_relative(stripped) == stripped, "Unsafe ZIP member")
                mode = entry.external_attr >> 16
                _require(entry.create_system == 3 and
                         stat.S_IFMT(mode) == (stat.S_IFDIR if is_dir else stat.S_IFREG) and
                         not entry.flag_bits & 1 and not entry.extra and not entry.comment and
                         entry.volume == 0,
                         "Unsupported ZIP member attributes")
                if is_dir:
                    _require(name in dirs and entry.file_size == entry.compress_size == 0 and
                             entry.compress_type == zipfile.ZIP_STORED, "Unexpected ZIP directory")
                    continue
                _require(name in expected and entry.file_size == expected[name]["bytes"] and
                         entry.file_size <= LIMITS["max_member_bytes"] and
                         entry.compress_type == zipfile.ZIP_DEFLATED, "ZIP file size/type mismatch")
                digest, count = hashlib.sha256(), 0
                with packed.open(entry) as raw:
                    while block := raw.read(CHUNK):
                        deadline.check()
                        count += len(block)
                        _require(count <= expected[name]["bytes"], "Expanded ZIP member overrun")
                        digest.update(block)
                _require(count == expected[name]["bytes"] and digest.hexdigest() == expected[name]["sha256"],
                         "ZIP member differs from original source identity")
            _require(seen == set(expected) | dirs, "ZIP inventory differs from source")
        _finish_open(archive, stream, before)
    finally:
        stream.close()


def _inputs(args, deadline):
    archive, manifest = _path(args.archive), _path(args.manifest)
    value, manifest_pin = _load(manifest, deadline)
    snapshot = _manifest_snapshot(value)
    _require(_artifact_pin(_path(value["artifact_receipt"]["path"]), snapshot, deadline) ==
             value["artifact_receipt"], "Original artifact receipt identity mismatch")
    _require(value["archive"]["path"] == _rel(archive), "Archive path differs from manifest")
    archive_pin = _hash(archive, deadline, LIMITS["max_archive_bytes"])
    _require(archive_pin == value["archive"], "Archive pin mismatch")
    _verify_payload(archive, snapshot, deadline)
    _require(_hash(archive, deadline, LIMITS["max_archive_bytes"]) == archive_pin,
             "Archive changed during verification")
    _require(_hash(manifest, deadline, MAX_JSON) == manifest_pin, "Manifest changed during verification")
    return value, snapshot, archive_pin, manifest_pin


def _pack(args, deadline):
    source, archive, manifest = _path(args.source), _path(args.archive), _path(args.manifest)
    artifact_receipt = _path(args.artifact_receipt)
    for output in (archive, manifest, _path(args.receipt), _start_path(_path(args.receipt))):
        _require(not _inside(output, source), "Retention outputs cannot be inside source")
    _require(not _inside(source, archive) and not _inside(source, manifest), "Overlapping source/output paths")
    _require(artifact_receipt not in (archive, manifest), "Output overlaps original artifact receipt")
    _fresh((archive, manifest))
    before = _snapshot(source, deadline)
    original_pin = _artifact_pin(artifact_receipt, before, deadline)
    _pack_zip(source, archive, before, deadline)
    archive_pin = _hash(archive, deadline, LIMITS["max_archive_bytes"])
    _verify_payload(archive, before, deadline)
    after = _snapshot(source, deadline)
    _require(before == after, "Source tree changed between full before/after snapshots")
    _require(_artifact_pin(artifact_receipt, after, deadline) == original_pin,
             "Original artifact receipt changed during retention")
    _require(_hash(archive, deadline, LIMITS["max_archive_bytes"]) == archive_pin,
             "Archive changed during pack verification")
    value = {"schema": SCHEMA, "scope": "lossless_retention_only", "acceptance_claim": False,
             "limits": LIMITS, "source_snapshot": before,
             "source_snapshot_sha256": _snapshot_digest(before), "source_unchanged_before_after": True,
             "archive": archive_pin, "artifact_receipt": original_pin,
             "tool": _hash(Path(__file__).resolve(), deadline, MAX_JSON)}
    _manifest_snapshot(value)
    _write_json(manifest, value)
    return {"archive": archive_pin, "manifest": _hash(manifest, deadline, MAX_JSON),
            "source_snapshot_sha256": value["source_snapshot_sha256"],
            "source_file_count": before["file_count"], "source_bytes": before["source_bytes"],
            "source_preserved": True, "restoration_performed": False}


def _verify(args, deadline):
    value, snapshot, archive_pin, manifest_pin = _inputs(args, deadline)
    checked_source = False
    if args.source is not None:
        source = _path(args.source)
        _require(_rel(source) == snapshot["source"], "Source path differs from manifest")
        _require(_snapshot(source, deadline) == snapshot, "Live source no longer matches packed snapshot")
        _require(_artifact_pin(_path(value["artifact_receipt"]["path"]), snapshot, deadline) ==
                 value["artifact_receipt"], "Original artifact receipt pin mismatch")
        checked_source = True
    return {"archive": archive_pin, "manifest": manifest_pin,
            "source_snapshot_sha256": value["source_snapshot_sha256"],
            "source_file_count": snapshot["file_count"], "source_bytes": snapshot["source_bytes"],
            "live_source_checked": checked_source, "restoration_performed": False}


def _restore(args, deadline):
    value, snapshot, archive_pin, manifest_pin = _inputs(args, deadline)
    subtree = _relative(args.subtree, dot=True)
    source, destination = _path(snapshot["source"]), _path(args.destination)
    _require(not _inside(destination, source) and not _inside(source, destination),
             "Restoration must be separate from original source")
    for input_path in (archive_pin["path"], manifest_pin["path"], args.receipt):
        _require(not _inside(_path(input_path), destination), "Restore inputs/receipt cannot be inside destination")
    _require(not _inside(_start_path(_path(args.receipt)), destination), "Running receipt inside destination")
    _require(subtree == "." or subtree in {row["member"] for row in snapshot["directories"]},
             "Selected subtree is not a recorded source directory")
    prefix = "" if subtree == "." else subtree + "/"
    files = [row for row in snapshot["files"] if row["member"].startswith(prefix)]
    directories = [row for row in snapshot["directories"] if row["member"].startswith(prefix)]
    count = sum(row["bytes"] for row in files)
    _require(files and count <= LIMITS["max_restore_bytes"], "Selected restore exceeds 256 MiB or is empty")
    _fresh((destination,))
    _parents(destination)
    destination.mkdir()
    restored = []
    stream, before = _opened(_path(archive_pin["path"]))
    try:
        with zipfile.ZipFile(stream) as packed:
            for row in sorted(directories, key=lambda row: (row["member"].count("/"), row["member"])):
                deadline.check()
                relative = row["member"][len(prefix):]
                if relative:
                    path = destination.joinpath(*PurePosixPath(relative).parts)
                    _chain(path.parent)
                    path.mkdir()
            for row in files:
                deadline.check()
                relative = row["member"][len(prefix):]
                path = destination.joinpath(*PurePosixPath(relative).parts)
                _chain(path)
                digest, size = hashlib.sha256(), 0
                with packed.open(row["member"]) as raw, path.open("xb") as output:
                    while block := raw.read(CHUNK):
                        deadline.check()
                        size += len(block)
                        _require(size <= row["bytes"], "Restored member exceeds recorded size")
                        output.write(block)
                        digest.update(block)
                    output.flush()
                    os.fsync(output.fileno())
                _require(size == row["bytes"] and digest.hexdigest() == row["sha256"],
                         "Restored member identity mismatch")
                restored.append({"source_path": row["source_path"], "member": row["member"],
                                 "restored_path": _rel(path), "bytes": size, "sha256": digest.hexdigest()})
        _finish_open(_path(archive_pin["path"]), stream, before)
    finally:
        stream.close()
    actual = _snapshot(destination, deadline)
    expected_files = {row["member"][len(prefix):]: (row["bytes"], row["sha256"]) for row in files}
    actual_files = {row["member"]: (row["bytes"], row["sha256"]) for row in actual["files"]}
    expected_dirs = {row["member"][len(prefix):] for row in directories if row["member"][len(prefix):]}
    _require(actual_files == expected_files and
             {row["member"] for row in actual["directories"]} == expected_dirs,
             "Restored tree differs from selected original subtree")
    _require(_hash(_path(archive_pin["path"]), deadline, LIMITS["max_archive_bytes"]) == archive_pin and
             _hash(_path(manifest_pin["path"]), deadline, MAX_JSON) == manifest_pin,
             "Retention inputs changed during restoration")
    return {"archive": archive_pin, "manifest": manifest_pin,
            "original_source": snapshot["source"], "subtree": subtree, "destination": _rel(destination),
            "restored_file_count": len(files), "restored_bytes": count, "restored": restored,
            "original_filesystem_identities_recreated": False, "reader_or_product_execution_performed": False}


def _start_path(receipt):
    return receipt.with_name(receipt.name + ".started.json")


def _parser():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="operation", required=True)
    for operation in ("pack", "verify", "restore"):
        command = commands.add_parser(operation)
        command.add_argument("--archive", required=True)
        command.add_argument("--manifest", required=True)
        command.add_argument("--receipt", required=True)
        if operation == "pack":
            command.add_argument("--source", required=True)
            command.add_argument("--artifact-receipt", required=True)
        elif operation == "verify":
            command.add_argument("--source", help="Also verify original live source byte/stat snapshot")
        else:
            command.add_argument("--subtree", required=True, help="Directory relative to source; '.' is bounded too")
            command.add_argument("--destination", required=True)
    return parser


def main():
    args = _parser().parse_args()
    deadline = Deadline()
    receipt = _path(args.receipt)
    start = _start_path(receipt)
    _fresh((receipt, start))
    for name in ("archive", "manifest", "artifact_receipt"):
        if hasattr(args, name):
            _require(_path(getattr(args, name)) not in (receipt, start), "Receipt overlaps input/output")
    if getattr(args, "source", None):
        source = _path(args.source)
        _require(not _inside(receipt, source) and not _inside(start, source), "Receipt inside original evidence")
    if getattr(args, "destination", None):
        destination = _path(args.destination)
        _require(not _inside(receipt, destination) and not _inside(start, destination), "Receipt inside restored tree")
    base = {"schema": SCHEMA, "operation": args.operation, "scope": "lossless_retention_only",
            "acceptance_claim": False, "limits": LIMITS, "arguments": vars(args),
            "original_runtime_or_storage_failure_reclassified": False}
    _write_json(start, {**base, "state": "running"})
    try:
        result = {"pack": _pack, "verify": _verify, "restore": _restore}[args.operation](args, deadline)
        deadline.check()
        state = {"pack": "retained_verified", "verify": "archive_verified", "restore": "restored_verified"}[args.operation]
        _write_json(receipt, {**base, "state": state, "elapsed_seconds": deadline.elapsed(), "result": result})
        print(json.dumps({"receipt": _rel(receipt), "state": state, "operation": args.operation},
                         separators=(",", ":"), allow_nan=False))
        return 0
    except BaseException as error:
        _write_json(receipt, {**base, "state": "failed", "elapsed_seconds": deadline.elapsed(),
                             "error_type": type(error).__name__, "error": str(error),
                             "partial_outputs_preserved": True, "source_deletion_performed": False})
        print(f"Retention {args.operation} failed; inspect {_rel(receipt)}: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
