"""Capture three current installed/native observation routes, without acceptance.

The RGB executable must come from the caller's identified existing full native
build. It is not a distribution-installed executable and reads its unchanged
committed reference via its compiled source path. The caller owns that build
identity; this helper records executable/source pins before and after capture.

RGB runs all 504 rows explicitly. Gaussian H1 runs twelve cancellation cases and
R16 runs one resource case. Their unchanged independent Python readers decide
acceptance. This helper never creates historical Gaussian references/contracts.
The original capture directories remain available for those readers, including
embedded absolute preference/raw paths. No capture is rewritten or deleted.

The 1200-second RGB and 180-second per-Gaussian deadlines are separate from the
complete Python suite's 2400-second deadline. Commands execute serially under
the Windows Job/Linux subreaper or macOS cooperative owner. File bounds are
post-exit transport admission, not a live disk quota or an RSS guarantee.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import stat
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "imagejxx.current-observations.001"
RGB_REFERENCE = "tests/fixtures/rgb-contrast-combined-imagej154r.json"
RGB_FIELDS = ("axis", "origin", "first_channels", "second_channels", "automatic")
H1_IDS = tuple(f"H1-{number:02d}" for number in range(1, 13))
R16_ID = "P02a.shared_ui.20.busy-failure-cancel-atomicity.held-preview-lease-lowered-budget"
MAX_REPORT_BYTES = 64 * 1024 * 1024
MAX_CAPTURE_BYTES = 512 * 1024 * 1024
MAX_FILES = 8192
SOURCE_INPUTS = (
    RGB_REFERENCE, "tests/native/test_rgb_combined_input.cpp",
    "tests/python/test_rgb_combined_input.py", "tests/python/test_gaussian_cancellation.py",
    "tests/python/test_gaussian_resource_ui.py", "src/desktop/main.cpp",
    "src/desktop/gaussian_cancellation_consumer.cpp", "src/desktop/gaussian_resource_consumer.cpp",
)


def checked_path(value):
    path = Path(os.path.abspath(value))
    for parent in (*reversed(path.parents), path):
        try:
            attributes = parent.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400:
            raise ValueError(f"Linked observation path/ancestor is not admitted: {parent}")
    return path


def regular(path):
    path = checked_path(path)
    if not stat.S_ISREG(path.lstat().st_mode):
        raise ValueError(f"Expected a regular observation input/member: {path}")
    return path


def pin(path):
    path = regular(path)
    digest, size = hashlib.sha256(), 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            size += len(block)
            digest.update(block)
    return {"bytes": size, "sha256": digest.hexdigest()}


def save(path, value):
    raw = (json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n").encode("utf-8")
    temporary = path.with_name(path.name + ".writing")
    with temporary.open("xb") as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def read_report(path):
    path = regular(path)
    if not 0 < path.stat().st_size <= MAX_REPORT_BYTES:
        raise ValueError("Current observation JSON exceeds its transport byte bound")

    def unique(pairs):
        row = {}
        for key, value in pairs:
            if key in row:
                raise ValueError("Duplicate current observation JSON key")
            row[key] = value
        return row

    def invalid(value):
        raise ValueError("Nonfinite current observation JSON token: " + value)

    value = json.loads(path.read_bytes(), object_pairs_hook=unique, parse_constant=invalid)
    pending, nodes = [(value, 0)], 0
    while pending:
        item, depth = pending.pop()
        nodes += 1
        if nodes > 2_000_000 or depth > 64:
            raise ValueError("Current observation JSON exceeds its structure bound")
        if isinstance(item, float) and not math.isfinite(item):
            raise ValueError("Nonfinite current observation JSON number")
        if isinstance(item, dict):
            pending.extend((child, depth + 1) for child in item.values())
        elif isinstance(item, list):
            pending.extend((child, depth + 1) for child in item)
    return value


def rgb_identity(row):
    if not isinstance(row, dict) or any(type(row.get(name)) is not int for name in RGB_FIELDS):
        raise ValueError("RGB observation identity requires the five integer case fields")
    return tuple(row[name] for name in RGB_FIELDS)


def rgb_identities(root):
    reference = read_report(root / RGB_REFERENCE)
    selected = [row for row in reference["cases"] if row["roi"] == 8]
    identifiers = [rgb_identity(row) for row in selected]
    if len(identifiers) != 504 or len(set(identifiers)) != 504:
        raise ValueError("The current RGB fixture no longer declares the original 504 identities")
    return set(identifiers)


def inventory(directory):
    """Hash retained producer bytes, without interpreting expected pixels."""
    directory = checked_path(directory)
    if not directory.is_dir():
        raise ValueError("Current observation producer did not create its directory")
    rows, size = [], 0

    def fail(error):
        raise error

    for parent, directories, names in os.walk(directory, followlinks=False, onerror=fail):
        for name in directories:
            path = checked_path(Path(parent) / name)
            if not path.is_dir():
                raise ValueError("Unexpected non-directory observation member")
        for name in names:
            path = regular(Path(parent) / name)
            size += path.stat().st_size
            if len(rows) >= MAX_FILES or size > MAX_CAPTURE_BYTES:
                raise ValueError("Current observation capture exceeds its transport inventory bound")
            rows.append({"path": path.relative_to(directory).as_posix(), **pin(path)})
    return sorted(rows, key=lambda row: row["path"])


def envelope(kind, path, expected_rgb):
    """Only case membership/file presence, never an acceptance oracle."""
    row = read_report(path)
    if not isinstance(row, dict):
        raise ValueError("Current observation report must be an object")
    if kind == "rgb-combined":
        rows = row.get("results")
        if (row.get("state") != "passed" or type(row.get("cases")) is not int or row["cases"] != 504 or
                not isinstance(rows, list) or len(rows) != 504):
            raise ValueError("RGB producer did not emit all 504 current observation rows")
        identifiers = [rgb_identity(item) for item in rows]
        if len(set(identifiers)) != 504 or set(identifiers) != expected_rgb:
            raise ValueError("RGB producer observation identities differ from the original fixture")
        members = []
        for item in rows:
            member = item.get("pixels_file")
            if (not isinstance(member, str) or not member.endswith("-applied.bin") or
                    Path(member).name != member or any(char in member for char in ("/", "\\", ":", "\0"))):
                raise ValueError("RGB pixel observation must be a plain relative member")
            regular(path.parent / member)
            members.append(member)
        if len(set(members)) != 504:
            raise ValueError("RGB producer reused a pixel observation member")
        return {"cases": 504, "producer_state": row["state"], "acceptance_claim": False}
    rows = row.get("cases")
    if (row.get("schema_version") != 1 or row.get("context") != "imgui_cpp" or
            row.get("status") != "passed" or row.get("acceptance_claim") is not False or
            not isinstance(rows, list)):
        raise ValueError("Unexpected current Gaussian capture envelope")
    expected = H1_IDS if kind == "gaussian-cancellation" else (R16_ID,)
    if (len(rows) != len(expected) or any(not isinstance(item, dict) for item in rows) or
            sorted(item.get("case_id", "") for item in rows) != sorted(expected) or
            any(item.get("context") != "imgui_cpp" for item in rows)):
        raise ValueError("Current Gaussian capture has missing, duplicate or foreign case identities")
    if kind == "gaussian-resource" and row.get("case_id") != R16_ID:
        raise ValueError("Current Gaussian resource capture changed its root case identity")
    return {"cases": len(expected), "case_ids": list(expected),
            "producer_state": row["status"], "acceptance_claim": False}


def load_owner():
    name = "ci_mac_process" if sys.platform == "darwin" else "ci_owned_process"
    path = regular(Path(__file__).with_name(name + ".py"))
    spec = importlib.util.spec_from_file_location("imagejxx_current_" + name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.run_mac_owned if sys.platform == "darwin" else module.run_owned


def deadline(value, maximum):
    if type(value) not in (int, float) or not math.isfinite(value) or not 0 < value <= maximum:
        raise ValueError("Current observation timeout must be finite, positive and within its fixed deadline")
    return float(value)


def capture_current(app, rgb_executable, output, environment, *, root=None,
                    rgb_timeout=1200, gaussian_timeout=180):
    """Return a capture-only receipt plus the three original reader paths.

    Root integrates this result with the full runner. Only a complete successful
    transport exposes reader_environment; failures preserve all bytes/logs and
    the terminal owned-process receipt without publishing a substitute report.
    """
    root = checked_path(ROOT if root is None else root)
    app, rgb_executable = regular(app), regular(rgb_executable)
    output = checked_path(output)
    rgb_timeout, gaussian_timeout = deadline(rgb_timeout, 1200), deadline(gaussian_timeout, 180)
    if not root.is_dir() or output == root or not output.is_relative_to(root) or output.exists():
        raise ValueError("Use a fresh owned observation output strictly below the explicit repository root")
    if app.is_relative_to(output) or rgb_executable.is_relative_to(output):
        raise ValueError("Observation output must not contain its input executables")
    if (not isinstance(environment, dict) or any(not isinstance(key, str) or not key or "=" in key or "\0" in key or
            not isinstance(value, str) or "\0" in value for key, value in environment.items())):
        raise ValueError("An explicit string-to-string child environment is required")
    child_environment = dict(environment, PYTHONDONTWRITEBYTECODE="1")
    output.mkdir(parents=True)
    receipt = {"schema": SCHEMA, "state": "admitting", "acceptance_claim": False,
               "scope": "504 current RGB and 12 H1 plus 1 R16 C++ observations",
               "root": str(root), "output": str(output), "commands": [], "reader_environment": {},
               "captures_retained": True, "environment_values_recorded": False,
               "historical_gaussian_reference_prepared": False}
    receipt_path = output / "receipt.json"
    save(receipt_path, receipt)
    source_paths = [root / name for name in SOURCE_INPUTS]
    helper_paths = [Path(__file__), Path(__file__).with_name("ci_owned_process.py")]
    if sys.platform == "darwin":
        helper_paths.append(Path(__file__).with_name("ci_mac_process.py"))
    inputs = [app, rgb_executable, *source_paths, *helper_paths]

    def pins():
        return [{"path": str(path), **pin(path)} for path in inputs]

    try:
        if sys.platform not in ("win32", "linux", "darwin"):
            raise ValueError("Unsupported current-observation ownership platform")
        before = pins()
        expected_rgb = rgb_identities(root)
        receipt["inputs_before"] = before
        receipt["state"] = "capturing"
        save(receipt_path, receipt)
        execute = load_owner()
        routes = (
            ("rgb-combined", [rgb_executable, output / "rgb-combined", "504"], rgb_timeout,
             "results.json", "IMAGEJXX_RGB_COMBINED_INPUT_REPORT"),
            ("gaussian-cancellation", [app, "--gaussian-cancellation-consumer-test", "--consumer-dir",
                                       output / "gaussian-cancellation"], gaussian_timeout,
             "imgui-cpp.json", "IMAGEJXX_H1_CPP_REPORT"),
            ("gaussian-resource", [app, "--gaussian-resource-consumer-test", "--consumer-dir",
                                   output / "gaussian-resource"], gaussian_timeout,
             "imgui-cpp.json", "IMAGEJXX_R16_CPP_REPORT"),
        )
        reader_environment = {}
        total_files, total_bytes = 0, 0
        for kind, arguments, timeout, report_name, reader_variable in routes:
            capture = output / kind
            if capture.exists() or pins() != before:
                raise ValueError("Capture already exists or a current producer/source input changed before launch")
            argv = [str(value) for value in arguments]
            command = {"kind": kind, "argv": argv, "timeout_seconds": timeout, "state": "running"}
            receipt["commands"].append(command)
            save(receipt_path, receipt)
            started = time.monotonic()
            log_path, process_path = output / (kind + ".log"), output / (kind + ".process.json")
            try:
                with log_path.open("xb") as log:
                    process = execute(argv, cwd=root, env=child_environment, timeout=timeout,
                                      stdout=log, stderr=subprocess.STDOUT, receipt_path=process_path)
                command.update(exit_code=process.get("exit_code"), process_state=process.get("state"),
                               cleanup_complete=process.get("cleanup_complete"))
                if (process.get("state") != "exited" or process.get("target_completed") is not True or
                        process.get("cleanup_complete") is not True or process.get("exit_code") != 0):
                    raise ValueError("Current observation command lacks a successful resolved owned exit: " + kind)
                report_path = capture / report_name
                observed = envelope(kind, report_path, expected_rgb)
                files = inventory(capture)
                total_files += len(files)
                total_bytes += sum(member["bytes"] for member in files)
                if total_files > MAX_FILES or total_bytes > MAX_CAPTURE_BYTES:
                    raise ValueError("Combined current observation captures exceed their transport bound")
                save(output / (kind + ".files.json"), files)
                command.update(state="captured", observation=observed,
                               report={"path": str(report_path), **pin(report_path)},
                               inventory={"path": kind + ".files.json", **pin(output / (kind + ".files.json"))})
                reader_environment[reader_variable] = str(report_path)
            except BaseException as error:
                command.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
                observed = getattr(error, "receipt", None)
                if isinstance(observed, dict):
                    command.update(process_state=observed.get("state"), cleanup_complete=observed.get("cleanup_complete"))
                raise
            finally:
                command["seconds"] = time.monotonic() - started
                for field, path in (("log", log_path), ("process", process_path)):
                    if path.exists():
                        command[field] = {"path": path.name, **pin(path)}
                save(receipt_path, receipt)
        after = pins()
        receipt["inputs_after"] = after
        if after != before:
            raise ValueError("A current producer/source input changed during capture")
        receipt.update(state="captured", reader_environment=reader_environment,
                       captured_files=total_files, captured_bytes=total_bytes)
    except BaseException as error:
        receipt.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        save(receipt_path, receipt)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--rgb-executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--rgb-timeout", type=float, default=1200)
    parser.add_argument("--gaussian-timeout", type=float, default=180)
    args = parser.parse_args()
    capture_current(args.app, args.rgb_executable, args.output, dict(os.environ), root=args.root,
                    rgb_timeout=args.rgb_timeout, gaussian_timeout=args.gaussian_timeout)


if __name__ == "__main__":
    main()
