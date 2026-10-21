"""Capture existing C++ UI probes for independent Python test readers.

Each process gets private preferences and a fresh capture directory. Reports are
copied byte for byte into the readers' directory layout; originals stay alive
because some reports contain absolute artifact paths. Captures are observations,
not acceptance verdicts. The Python readers supply the independent assertions.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import zipfile


def pin(path):
    path = Path(path)
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return {"bytes": path.stat().st_size, "sha256": digest.hexdigest()}


def cases():
    groups = [
        ("rank", "--rank-ui-case", [f"R{i:02d}" for i in range(1, 60)]),
        ("selection", "--selection-creation-ui-case", ["R01", "R02", "R03"]),
        ("rounded-export", "--rounded-roi-export-ui-case",
         ["axis", "rotated", "oversized", "zero", "precision"]),
        ("legacy-export", "--roi-export-ui-case", [None]),
        ("rounded-import", "--rounded-roi-import-ui-case", ["axis", "rotated", "zero", "oversized"]),
        ("legacy-replacement", "--roi-import-replacement-ui-case", [None]),
        ("rounded-legacy", "--rounded-legacy-ui-case", ["axis", "rotated"]),
        ("saved-budget", "--saved-roi-budget-ui-case",
         ["finite-axis", "finite-rotated", "ieee-axis", "ieee-rotated"]),
        ("saved-pointer", "--saved-roi-pointer-budget-ui-case",
         ["finite-axis", "finite-rotated", "ieee-axis", "ieee-rotated"]),
        ("saved-modifiers", "--saved-roi-pointer-budget-ui-case",
         [case + "-modifiers" for case in ["finite-axis", "finite-rotated", "ieee-axis", "ieee-rotated"]]),
    ]
    for group, flag, variants in groups:
        for variant in variants:
            yield group, variant or "legacy", [flag] + ([variant] if variant else [])


def reader_environment(output):
    reports = Path(output).resolve() / "reports"
    environment = {
        "IMAGEJXX_ROUNDED_CPP_CASE": reports / "selection/R01",
        "IMAGEJXX_ROUNDED_SAVED_CPP_BASE": reports / "selection",
        "IMAGEJXX_ROUNDED_EXPORT_CPP_BASE": reports / "rounded-export",
        "IMAGEJXX_ROI_EXPORT_CPP_REPORT": reports / "legacy-export/legacy/case.json",
        "IMAGEJXX_ROUNDED_IMPORT_CPP_BASE": reports / "rounded-import",
        "IMAGEJXX_ROI_REPLACEMENT_CPP_REPORT": reports / "legacy-replacement/legacy/case.json",
        "IMAGEJXX_ROUNDED_LEGACY_CPP_BASE": reports / "rounded-legacy",
        "IMAGEJXX_SAVED_BUDGET_CPP_BASE": reports / "saved-budget",
        "IMAGEJXX_SAVED_POINTER_CPP_BASE": reports / "saved-pointer",
        "IMAGEJXX_SAVED_MODIFIERS_CPP_BASE": reports / "saved-modifiers",
        "IMAGEJXX_VARIANCE_CPP_REPORT": reports / "rank/R14/case.json",
    }
    for name in ("NONBYTE", "THRESHOLD", "CHANNEL_VARIANCE", "DESPECKLE",
                 "OUTLIERS", "REMOVE_NANS", "TOPHAT", "IEEE_REMOVE_NANS"):
        environment[f"IMAGEJXX_{name}_CPP_DIR"] = reports / "rank"
    return {name: str(path) for name, path in environment.items()}


def inventory(directory):
    result = []
    size = 0
    for path in sorted(directory.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"Unexpected symlink in C++ capture: {path}")
        if not path.is_file():
            continue
        size += path.stat().st_size
        if size > 256 * 1024 * 1024 or len(result) >= 4096:
            raise ValueError(f"C++ capture exceeds retention budget: {directory}")
        result.append({"member": path.relative_to(directory).as_posix(), **pin(path)})
    return result


def capture(output, executable, root, environment=None):
    output = Path(output).resolve()
    executable = Path(executable).resolve(strict=True)
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ValueError(f"C++ application is not executable: {executable}")
    if output.exists():
        raise ValueError(f"Use a fresh C++ capture directory: {output}")
    output.mkdir(parents=True)
    original_pin = pin(executable)
    receipt = {"state": "running", "acceptance_claim": False,
               "application": {"path": str(executable), **original_pin},
               "cases": [], "reader_environment": reader_environment(output)}

    def save():
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    save()
    try:
        for group, case, arguments in cases():
            # main.cpp places preferences next to the capture directory. Sharing
            # that parent across cases would leak settings between probes.
            private = output / "private" / group / case
            private.mkdir(parents=True)
            actual = private / "capture"
            transport = output / "reports" / group / case
            argv = [str(executable), *arguments, str(actual)]
            item = {"group": group, "case": case, "argv": argv, "state": "running",
                    "actual": str(actual), "transport": str(transport)}
            receipt["cases"].append(item)
            save()
            started = time.monotonic()
            print(f"C++ observation: {group}/{case}", flush=True)
            with (private / "process.log").open("wb") as log:
                process = subprocess.run(argv, cwd=root, env=environment, stdout=log,
                                         stderr=subprocess.STDOUT, timeout=120, check=False)
            item.update(exit_code=process.returncode, elapsed_seconds=time.monotonic() - started)
            if process.returncode:
                raise subprocess.CalledProcessError(process.returncode, argv)
            report = actual / "case.json"
            if not report.is_file() or report.stat().st_size > 128 * 1024 * 1024:
                raise ValueError(f"Missing or oversized observation report: {report}")
            observation = json.loads(report.read_bytes())
            if observation.get("observation_state", observation.get("state")) != "observed":
                raise ValueError(f"C++ observation did not finish: {report}")
            if observation.get("acceptance_claim") is not False:
                raise ValueError(f"C++ observation must not claim acceptance: {report}")
            item["files"] = inventory(actual)
            # Do not link captures: hard links change file identity/link counts,
            # and symlinks require privileges on some supported hosts.
            shutil.copytree(actual, transport)
            if inventory(transport) != item["files"]:
                raise ValueError(f"C++ observation copy changed bytes: {transport}")
            item["state"] = "observed"
            save()
        if pin(executable) != original_pin:
            raise ValueError("C++ application changed during observation capture")
        receipt["state"] = "observed"
        return receipt["reader_environment"]
    except BaseException as error:
        receipt.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        if receipt["cases"] and receipt["cases"][-1]["state"] == "running":
            receipt["cases"][-1]["state"] = "failed"
        raise
    finally:
        save()


def archive(output):
    """Retain originals, transports, preferences and logs without rewriting JSON."""
    output = Path(output).resolve()
    destination = output.with_suffix(".zip")
    if destination.exists():
        raise ValueError(f"Refusing to replace capture archive: {destination}")
    with zipfile.ZipFile(destination, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=1) as bundle:
        for path in sorted(output.rglob("*")):
            if path.is_symlink():
                raise ValueError(f"Unexpected symlink in capture archive: {path}")
            if path.is_file():
                bundle.write(path, path.relative_to(output).as_posix())
    return {"path": str(destination), **pin(destination)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--application", type=Path, required=True)
    args = parser.parse_args()
    capture(args.output, args.application, Path(__file__).resolve().parents[1])
    print(json.dumps(archive(args.output), indent=2))


if __name__ == "__main__":
    main()
