"""Read-only evidence availability inventory; never a parity acceptance gate.

Original paths and producer hashes remain authoritative. A portable archive
locator can supply the exact bytes, but cannot change an original fingerprint.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat
import tarfile
import zipfile

MAX_BYTES = 512 * 1024 * 1024
MAX_ENTRIES = 16384
# The original inventory currently references 25,975 distinct paths, mostly
# retired per-case captures. Keep that baseline visible without loosening the
# separate archive-member bound.
MAX_REQUIREMENTS = 65536


def relative(value):
    if not isinstance(value, str):
        raise ValueError("Evidence path must be a string")
    parts = value.replace("\\", "/").split("/")
    if (not value or any(part in {"", ".", ".."} for part in parts)
            or ":" in value or value.startswith(("/", "\\"))):
        raise ValueError("Use an explicit workspace-relative evidence path")
    return PurePosixPath(*parts).as_posix()


def owned_file(root, value):
    """Inspect lexical parents before resolving; do not follow reparse points."""
    root = Path(root).resolve(strict=True)
    value = relative(value)
    path = root
    for part in PurePosixPath(value).parts:
        path = path / part
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("Evidence links and reparse points are not followed")
    if not path.is_file():
        raise ValueError("Evidence locator is not a regular file")
    return path


def fingerprint(stream, max_bytes=MAX_BYTES):
    size = 0
    digest = hashlib.sha256()
    while block := stream.read(min(1024 * 1024, max_bytes - size + 1)):
        size += len(block)
        if size > max_bytes:
            raise ValueError("Evidence exceeds the declared byte limit")
        digest.update(block)
    return {"bytes": size, "sha256": digest.hexdigest()}


def archive_member(path, member):
    member = relative(member)
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            rows = archive.infolist()
            if len(rows) > MAX_ENTRIES:
                raise ValueError("Archive entry limit exceeded")
            matches = [row for row in rows if row.filename == member]
            if len(matches) != 1:
                raise ValueError("Archive member is missing or ambiguous")
            row = matches[0]
            if (row.is_dir() or stat.S_ISLNK(row.external_attr >> 16)
                    or row.file_size > MAX_BYTES):
                raise ValueError("Archive member is not bounded regular evidence")
            with archive.open(row) as stream:
                return fingerprint(stream)
    with tarfile.open(path, "r:*") as archive:
        found = None
        for index, row in enumerate(archive):
            if index >= MAX_ENTRIES:
                raise ValueError("Archive entry limit exceeded")
            if row.name != member:
                continue
            if found is not None:
                raise ValueError("Archive member is ambiguous")
            if not row.isfile() or row.size > MAX_BYTES:
                raise ValueError("Archive member is not bounded regular evidence")
            with archive.extractfile(row) as stream:
                found = fingerprint(stream)
        if found is None:
            raise ValueError("Archive member is missing")
        return found


def inspect_entry(root, entry, retired_prefixes=()):
    allowed = {"requirement_id", "run_id", "role", "path", "sha256", "archive", "references"}
    if not isinstance(entry, dict) or set(entry) - allowed or "path" not in entry:
        raise ValueError("Unknown or incomplete evidence entry")
    original = relative(entry["path"])
    expected = entry.get("sha256")
    if expected is not None and not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError("Invalid original evidence SHA256")
    result = {**entry, "path": original, "availability": "unavailable",
              "identity": "not_checked", "state": "unavailable"}
    archive = entry.get("archive")
    try:
        if archive is not None:
            if not isinstance(archive, dict) or set(archive) != {"path", "member"}:
                raise ValueError("Archive locator requires exactly path and member")
            path = owned_file(root, archive["path"])
            actual = archive_member(path, archive["member"])
            result["locator"] = {"archive": relative(archive["path"]),
                                 "member": relative(archive["member"])}
        else:
            path = owned_file(root, original)
            with path.open("rb") as stream:
                actual = fingerprint(stream)
            result["locator"] = {"path": original}
        result.update(availability="present", actual=actual)
        if expected is None:
            result.update(state="not_checked", reason="No original hash supplied; presence is not identity proof")
        elif actual["sha256"] != expected:
            result.update(state="mismatch", identity="mismatch",
                          reason="Bytes differ from the original producer fingerprint")
        else:
            result.update(state="present_verified", identity="matched")
    except FileNotFoundError:
        retired = any(original == prefix or original.startswith(prefix + "/")
                      for prefix in retired_prefixes)
        result["reason"] = "retired" if retired else "missing_or_never_supplied"
    except PermissionError:
        result["reason"] = "inaccessible"
    except (OSError, ValueError, tarfile.TarError, zipfile.BadZipFile) as error:
        result.update(reason="invalid_locator_or_evidence", error=str(error))
    return result


def inventory_entries(inventory):
    """Retain each capability's exact file links and verification-run identity."""
    entries = []
    for row in inventory["capabilities"]:
        for role, paths in row.get("evidence", {}).items():
            for path in paths:
                entries.append({"requirement_id": row["id"], "role": role, "path": path})

    def visit(value, run_id, role=""):
        if isinstance(value, dict):
            for key, child in value.items():
                visit(child, run_id, f"{role}.{key}" if role else key)
        elif isinstance(value, list):
            for index, child in enumerate(value):
                visit(child, run_id, f"{role}[{index}]")
        elif isinstance(value, str) and value.startswith(("build/", ".tmp/")):
            # Only literal file references, not prose containing old paths.
            if not any(char.isspace() for char in value):
                entries.append({"run_id": run_id, "role": role, "path": value})

    for run in inventory.get("verification_runs", []):
        visit(run, run["id"])
    # The capability inventory repeats common files across many families. Hash
    # each locator once, retaining every requirement/run association explicitly.
    by_path = {}
    for row in entries:
        by_path.setdefault(row["path"], []).append(row)
    result = []
    for path, rows in by_path.items():
        if len(rows) == 1:
            result.append(rows[0])
        else:
            result.append({"path": path, "references": [
                {key: value for key, value in row.items() if key != "path"}
                for row in rows]})
    return result


def audit(root, entries, retired_prefixes=()):
    if len(entries) > MAX_REQUIREMENTS:
        raise ValueError("Evidence entry limit exceeded")
    prefixes = tuple(relative(prefix) for prefix in retired_prefixes)
    rows = [inspect_entry(root, entry, prefixes) for entry in entries]
    return {"schema_version": 1, "state": "observed", "acceptance": False,
            "complete_requirement_coverage": False,
            "counts": dict(Counter(row["state"] for row in rows)), "entries": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--entries", type=Path)
    parser.add_argument("--retired-prefix", action="append", default=[])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    entries = (json.loads(args.entries.read_bytes()) if args.entries else
               inventory_entries(json.loads((args.root / "docs/parity/inventory.json").read_bytes())))
    report = audit(args.root, entries, args.retired_prefix)
    raw = json.dumps(report, indent=2) + "\n"
    if args.output:
        with args.output.open("x", encoding="utf-8") as stream:
            stream.write(raw)
    else:
        print(raw, end="")


if __name__ == "__main__":
    main()
