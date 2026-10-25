"""Bounded, lossless retention of Python CI producer diagnostics.

Call only after the owning processes have stopped. The default selects producer
diagnostics. The explicit headless-tests profile snapshots all regular files in
the caller-admitted fresh test output; the caller owns its shape/cleanup checks.
An incomplete archive reports every observed omission and any discovery limit;
it is not product acceptance or a claim that unexamined data was absent.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import zipfile


SCHEMA = "imagejxx.ci-python-diagnostics.001"
CHUNK_BYTES = 64 * 1024
MANIFEST_MEMBER = "manifest.json"
NAMED_DIAGNOSTICS = {"runtime.json", "failure.json", "receipt.json", "case.json"}
IDENTITY_FIELDS = ("device", "inode", "bytes", "mtime_ns", "ctime_ns")


def _check_selection(selection):
    if type(selection) is not str or selection not in ("diagnostics", "headless-tests"):
        raise ValueError("Unknown Python diagnostic selection profile")


def _selected(name):
    return (name in NAMED_DIAGNOSTICS or name.endswith(".log")
            or name.endswith(".runtime.json") or name.endswith(".jsonl"))


def _linked(info):
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info, "st_file_attributes", 0) & 0x400)


def _absolute(path):
    # abspath removes lexical '..' without traversing a linked filesystem node.
    return Path(os.path.abspath(os.fspath(path)))


def _unlinked_ancestors(path):
    for entry in (*reversed(path.parents), path):
        info = entry.lstat()
        if _linked(info):
            raise ValueError("Diagnostic path has a symlink or junction: " + str(entry))
    return info


def _identity(info):
    return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def _path_handle_identity_matches(path_identity, handle_identity):
    # CPython 3.12.8 Windows stat/lstat copies creation time into st_ctime,
    # whereas fstat keeps FILE_BASIC_INFO.ChangeTime. These are different
    # clocks, not a tolerated timestamp drift. Compare their shared fields
    # here; _capture_source keeps all five fields for each API's own sequence.
    # https://github.com/python/cpython/blob/v3.12.8/Modules/posixmodule.c#L2006-L2036
    # https://github.com/python/cpython/blob/v3.12.8/Python/fileutils.c#L1026-L1045
    # https://github.com/python/cpython/blob/v3.12.8/Python/fileutils.c#L1147-L1195
    if sys.platform == "win32":
        return path_identity[:4] == handle_identity[:4]
    return path_identity == handle_identity


def _pin(path):
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(CHUNK_BYTES), b""):
            digest.update(block)
            size += len(block)
    return {"bytes": size, "sha256": digest.hexdigest()}


def _omission(path, reason, *, size=None, error=None):
    value = {"path": path, "reason": reason}
    if size is not None:
        value["bytes"] = size
    if error is not None:
        value["error"] = {"type": type(error).__name__, "message": str(error)}
        if isinstance(error, _SourceChanged):
            value["error"].update(stage=error.stage, identity_fields=list(IDENTITY_FIELDS),
                                  identities={name: list(identity) if identity is not None else None
                                              for name, identity in error.identities.items()},
                                  differing_fields=error.differing_fields)
    return value


def _discover(source, max_entries, *, selection="diagnostics"):
    _check_selection(selection)
    candidates, omitted, pending = [], [], [source]
    scanned = 0
    complete = True
    while pending:
        directory = pending.pop()
        relative = directory.relative_to(source).as_posix()
        try:
            info = _unlinked_ancestors(directory)
            if not stat.S_ISDIR(info.st_mode):
                omitted.append(_omission(relative, "directory_changed"))
                complete = False
                continue
            with os.scandir(directory) as entries:
                for entry in entries:
                    if scanned == max_entries:
                        # The current directory and queued subtrees have not
                        # been fully examined. Their file counts are unknown.
                        omitted.append(_omission(relative, "entry_limit"))
                        omitted.extend(_omission(path.relative_to(source).as_posix(), "unexamined_directory")
                                       for path in pending)
                        return candidates, omitted, scanned, False
                    scanned += 1
                    path = directory / entry.name
                    name = path.relative_to(source).as_posix()
                    try:
                        info = path.lstat()
                    except OSError as error:
                        omitted.append(_omission(name, "entry_unreadable", error=error))
                        complete = False
                        continue
                    if _linked(info):
                        omitted.append(_omission(name, "linked_path"))
                        complete = False
                    elif stat.S_ISDIR(info.st_mode):
                        pending.append(path)
                    elif selection == "headless-tests" or _selected(entry.name):
                        if stat.S_ISREG(info.st_mode):
                            candidates.append((name, path, info))
                        else:
                            omitted.append(_omission(name, "not_regular"))
                            complete = False
        except (OSError, ValueError) as error:
            omitted.append(_omission(relative, "directory_unreadable", error=error))
            complete = False
    return candidates, omitted, scanned, complete


class _SourceChanged(Exception):
    def __init__(self, message, *, stage, identities):
        self.stage = stage
        self.identities = dict(identities)
        expected = self.identities["expected"]
        self.differing_fields = [name for index, name in enumerate(IDENTITY_FIELDS)
                                 if any(value is not None and value[index] != expected[index]
                                        for value in self.identities.values())]
        super().__init__(message + "; stage=" + stage + "; identities=" + repr(self.identities))


def _capture_source(source, expected_info, spool, allowance):
    before = _unlinked_ancestors(source)
    identities = {"expected": _identity(expected_info), "path_before": _identity(before),
                  "open_before": None, "open_after": None, "path_after": None}
    if not stat.S_ISREG(before.st_mode) or identities["path_before"] != identities["expected"]:
        raise _SourceChanged("Source identity changed after discovery", stage="before_open", identities=identities)
    digest = hashlib.sha256()
    size = 0
    with source.open("rb") as stream:
        identities["open_before"] = _identity(os.fstat(stream.fileno()))
        if not _path_handle_identity_matches(identities["path_before"], identities["open_before"]):
            raise _SourceChanged("Opened source differs from the discovered regular file",
                                 stage="after_open", identities=identities)
        while True:
            block = stream.read(min(CHUNK_BYTES, allowance - size + 1))
            if not block:
                break
            size += len(block)
            if size > allowance:
                raise _SourceChanged("Source grew beyond its admitted byte allowance",
                                     stage="read", identities=identities)
            digest.update(block)
            spool.write(block)
        after_open = os.fstat(stream.fileno())
    after_path = _unlinked_ancestors(source)
    identities.update(open_after=_identity(after_open), path_after=_identity(after_path))
    if (identities["open_before"] != identities["open_after"]
            or identities["path_before"] != identities["path_after"]
            or not _path_handle_identity_matches(identities["path_after"], identities["open_after"])
            or size != before.st_size):
        raise _SourceChanged("Source changed during diagnostic capture", stage="after_read", identities=identities)
    spool.seek(0)
    return {"bytes": size, "sha256": digest.hexdigest()}


def _verify_archive(destination, manifest, manifest_bytes):
    expected = {row["member"]: {key: row[key] for key in ("bytes", "sha256")}
                for row in manifest["members"]}
    expected[MANIFEST_MEMBER] = {"bytes": len(manifest_bytes),
                                 "sha256": hashlib.sha256(manifest_bytes).hexdigest()}
    with zipfile.ZipFile(destination, "r") as bundle:
        rows = bundle.infolist()
        names = [row.filename for row in rows]
        if len(names) != len(set(names)) or set(names) != set(expected):
            raise ValueError("Diagnostic archive membership changed")
        for row in rows:
            wanted = expected[row.filename]
            if row.is_dir() or row.file_size != wanted["bytes"]:
                raise ValueError("Diagnostic archive member size changed")
            digest = hashlib.sha256()
            size = 0
            with bundle.open(row, "r") as stream:
                for block in iter(lambda: stream.read(CHUNK_BYTES), b""):
                    size += len(block)
                    if size > wanted["bytes"]:
                        raise ValueError("Diagnostic archive member exceeds its recorded size")
                    digest.update(block)
            if size != wanted["bytes"] or digest.hexdigest() != wanted["sha256"]:
                raise ValueError("Diagnostic archive member fingerprint changed")


def retain_python_diagnostics(source, destination, *, max_file_bytes=64 * 1024**2,
                              max_total_bytes=64 * 1024**2, max_files=1024, max_entries=16384,
                              selection="diagnostics"):
    """Snapshot selected stopped-producer diagnostics without altering sources.

    The destination must be fresh, outside ``source``, and have a real existing
    parent. Every admitted payload is retained whole and streamed back from the
    ZIP for verification. A single bounded spool avoids publishing a partially
    read or changing source as a retained member. Limits produce explicit
    omissions; a missing source produces ``unavailable`` without an archive.
    ``headless-tests`` retains all regular members of the caller-admitted fresh
    stopped test output. The default selection/manifest remain unchanged.
    """
    _check_selection(selection)
    limits = {"file_bytes": max_file_bytes, "total_bytes": max_total_bytes,
              "files": max_files, "entries": max_entries}
    if any(type(value) is not int or value < 1 for value in limits.values()):
        raise ValueError("Diagnostic limits must be positive integers")
    source, destination = _absolute(source), _absolute(destination)
    if destination == source or destination.is_relative_to(source):
        raise ValueError("Diagnostic destination must be outside its source tree")
    parent_info = _unlinked_ancestors(destination.parent)
    if not stat.S_ISDIR(parent_info.st_mode):
        raise ValueError("Diagnostic destination parent is not a directory")
    try:
        destination.lstat()
    except FileNotFoundError:
        pass
    else:
        raise ValueError("Diagnostic destination already exists or aliases an existing path")
    manifest = {"schema": SCHEMA, "acceptance_claim": False, "state": "complete",
                "source_root": str(source), "limits": limits,
                "discovery": {"complete": True, "entries_scanned": 0},
                "matched_files": 0, "retained_bytes": 0, "members": [], "omitted": []}
    if selection == "headless-tests":
        manifest["selection"] = selection
    try:
        source_info = _unlinked_ancestors(source)
    except FileNotFoundError:
        manifest.update(state="unavailable", archive=None, manifest=None)
        manifest["discovery"]["complete"] = False
        manifest["omitted"].append(_omission(".", "source_missing"))
        return manifest
    except OSError as error:
        source_info = None
        manifest["discovery"]["complete"] = False
        manifest["omitted"].append(_omission(".", "source_unreadable", error=error))
    if source_info is not None and not stat.S_ISDIR(source_info.st_mode):
        raise ValueError("Diagnostic source must be a real directory")
    if source_info is not None:
        candidates, omissions, scanned, complete = _discover(source, max_entries, selection=selection)
        manifest["discovery"] = {"complete": complete, "entries_scanned": scanned}
        manifest["omitted"].extend(omissions)
    else:
        candidates = []
    manifest["matched_files"] = len(candidates)
    # Exclusive creation also refuses a destination that appeared after the
    # initial check. No prior artifact is replaced, including a linked alias.
    with destination.open("xb") as archive_file:
        with zipfile.ZipFile(archive_file, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=6, allowZip64=True) as bundle:
            for name, path, observed in sorted(candidates, key=lambda row: row[0]):
                if len(manifest["members"]) == max_files:
                    manifest["omitted"].append(_omission(name, "file_count_limit", size=observed.st_size))
                    continue
                if observed.st_size > max_file_bytes:
                    manifest["omitted"].append(_omission(name, "file_size_limit", size=observed.st_size))
                    continue
                remaining = max_total_bytes - manifest["retained_bytes"]
                if observed.st_size > remaining:
                    manifest["omitted"].append(_omission(name, "total_size_limit", size=observed.st_size))
                    continue
                with tempfile.TemporaryFile(mode="w+b", dir=destination.parent) as spool:
                    try:
                        identity = _capture_source(path, observed, spool, min(max_file_bytes, remaining))
                    except (OSError, ValueError, _SourceChanged) as error:
                        reason = "source_changed" if isinstance(error, _SourceChanged) else "source_unreadable"
                        manifest["omitted"].append(_omission(name, reason, size=observed.st_size, error=error))
                        continue
                    member = "diagnostics/" + name
                    with bundle.open(member, "w", force_zip64=True) as output:
                        for block in iter(lambda: spool.read(CHUNK_BYTES), b""):
                            output.write(block)
                    manifest["members"].append({"path": name, "member": member, **identity})
                    manifest["retained_bytes"] += identity["bytes"]
            if manifest["omitted"] or not manifest["discovery"]["complete"]:
                manifest["state"] = "incomplete"
            manifest_bytes = (json.dumps(manifest, ensure_ascii=True, indent=2) + "\n").encode("utf-8")
            bundle.writestr(MANIFEST_MEMBER, manifest_bytes)
    _verify_archive(destination, manifest, manifest_bytes)
    return {**manifest, "archive": {"path": str(destination), **_pin(destination)},
            "manifest": {"member": MANIFEST_MEMBER, "bytes": len(manifest_bytes),
                         "sha256": hashlib.sha256(manifest_bytes).hexdigest()}}
