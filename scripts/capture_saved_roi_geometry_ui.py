"""Capture the adopted P06g paired cases from existing installed products.

The child mode is the real installed Python launcher, including loaded-module
origins before/after the native probe. The parent reuses the bounded subprocess
runner, preserves producer bytes and prepares inputs for the independent reader.
No expected geometry or acceptance decision is produced here.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
CASES = tuple(f"PX{i:02}" for i in range(1, 11))
CONTEXTS = ("imgui_cpp", "imgui_python")
SOURCE_ROLES = {
    "producer": "src/desktop/saved_roi_geometry_ui_probe.cpp",
    "producer_header": "src/desktop/saved_roi_geometry_ui_probe.hpp",
    "window": "src/desktop/window.cpp",
    "headless_header": "src/desktop/headless_session_internal.hpp",
    "draft": "src/desktop/saved_roi_edit.cpp",
    "draft_header": "src/desktop/saved_roi_edit.hpp",
    "selection_panel": "src/desktop/selection_panel.cpp",
    "linked_canvas": "src/desktop/orthogonal.cpp",
    "desktop_binding": "src/desktop/python.cpp",
    "python_wrapper": "src/imagejxx/ui/app.py",
    "reader": "tests/python/test_saved_roi_geometry_ui.py",
    "fixture": "tests/fixtures/saved_roi_geometry_owned_cases.json",
    "python_launcher": "scripts/capture_saved_roi_geometry_ui.py",
}
DOC_DELTA = {
    "docs/PLAN.md", "docs/IMPLEMENTATION.md", "docs/SOL_ULTRA.md",
    "docs/packets/P06g-positioned-scientific-xy.md",
}
MIB = 1024 * 1024


def pin(path: Path, *, allow_empty: bool = False) -> dict:
    path = path.absolute()
    size = path.stat().st_size
    if not (0 if allow_empty else 1) <= size <= 256 * MIB:
        raise ValueError(f"Invalid pinned member size: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(MIB), b""):
            digest.update(block)
    return {"path": str(path), "bytes": size, "sha256": digest.hexdigest()}


def write(path: Path, value: dict) -> None:
    raw = (json.dumps(value, ensure_ascii=False, allow_nan=False, indent=2) + "\n").encode("utf-8")
    if len(raw) > MIB:
        raise ValueError("Capture metadata exceeds1MiB")
    with path.open("xb") as stream:
        stream.write(raw)


def engine() -> Path:
    if os.name != "nt":
        raise RuntimeError("This installed-product capture is Windows-specific")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    get_name = kernel.GetModuleFileNameW
    get_name.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint32]
    get_name.restype = ctypes.c_uint32
    buffer = ctypes.create_unicode_buffer(32768)
    size = get_name(None, buffer, len(buffer))
    if not 0 < size < len(buffer):
        raise ctypes.WinError(ctypes.get_last_error())
    return Path(buffer.value)


def python_case(case: str, directory: Path, installed: Path) -> None:
    # Launch the qualified base engine directly so the supervisor owns the
    # actual process, rather than a venv redirector with an unowned child.
    # Select only the real installed environment here, never repository code.
    site_packages = installed / "Lib/site-packages"
    if not site_packages.is_dir():
        raise RuntimeError("Selected installed environment has no site-packages")
    sys.path.insert(0, str(site_packages))
    import imagejxx
    import imagejxx._native
    import imagejxx._desktop
    from imagejxx.ui.app import Editor
    from imagejxx.ui.persistence import Preferences

    modules = (imagejxx, imagejxx._native, imagejxx._desktop)
    package = Path(imagejxx.__file__).absolute().parent
    if not package.resolve().is_relative_to(installed.resolve()):
        raise RuntimeError("Python launcher did not load the selected installation")

    def origin():
        return {"executable": pin(engine()),
                "modules": {module.__name__: pin(Path(module.__file__)) for module in modules}}

    receipt = {"schema_version": 1, "kind": "p06g.saved-roi-python-launcher-origin.001",
               "case_id": case, "context": "imgui_python", "process_id": os.getpid(),
               "package_root": str(package), "before": origin()}
    try:
        editor = Editor(preferences=Preferences(directory.parent / "preferences.json"))
        editor._editor._saved_roi_geometry_ui_case(case, str(directory))
    finally:
        receipt["after"] = origin()
        write(directory.parent / "launcher-origin.json", receipt)
    if receipt["before"] != receipt["after"]:
        raise RuntimeError("Installed Python origins changed during the case")


def capture(output: Path, cpp_prefix: Path, python_prefix: Path, source_freeze: Path,
            build_report: Path, origin_report: Path) -> None:
    # The existing runner bounds combined logs to16MiB, the exclusively owned
    # stage to128MiB/8192 files, each subprocess and its10s terminal cleanup.
    sys.path.insert(0, str(ROOT / "scripts"))
    from capture_roi_boolean_reference import _run, _storage

    output = output.resolve()
    build_root = (ROOT / "build").resolve()
    if output == build_root or not output.is_relative_to(build_root):
        raise ValueError("Capture output must be an owned fresh workspace build child")
    output.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    report = {"schema_version": 1, "scope": "actual paired20 producer capture; independent reader required",
              "state": "running", "processes": [], "source_delta": [],
              "limits": {"parent_seconds": 300, "case_process_seconds": 20,
                         "context_process_seconds": 60, "context_archive_bytes": 64 * MIB,
                         "stage_bytes": 128 * MIB, "stage_files": 8192,
                         "combined_log_bytes": 16 * MIB, "cleanup_seconds": 10}}
    try:
        previous = source_freeze.resolve(strict=True)
        baseline = json.loads(previous.read_text(encoding="utf-8"))["sources"]
        for row in baseline:
            actual = pin(ROOT / row["path"], allow_empty=True)
            if any(actual[key] != row[key] for key in ("bytes", "sha256")):
                if row["path"] not in DOC_DELTA:
                    raise RuntimeError(f"Unreviewed consumed-source change: {row['path']}")
                report["source_delta"].append({"path": row["path"], "before": row, "after": actual})
        report["previous_freeze"] = pin(previous)
        report["unchanged_baseline_sources"] = len(baseline) - len(report["source_delta"])
        source_rows = []
        for role, logical in SOURCE_ROLES.items():
            source = ROOT / logical
            frozen = output / "source" / logical
            frozen.parent.mkdir(parents=True, exist_ok=True)
            with frozen.open("xb") as stream:
                stream.write(source.read_bytes())
            source_rows.append({"role": role, "logical_path": logical, **pin(frozen)})

        cpp = cpp_prefix / "bin/imagejxx.exe"
        python = engine()
        config = dict(line.split(" = ", 1) for line in
                      (python_prefix / "pyvenv.cfg").read_text(encoding="utf-8").splitlines()
                      if " = " in line)
        assert Path(config["home"]).resolve() == python.parent.resolve()
        report["python_engine"] = pin(python)
        report["python_launch_mode"] = "direct qualified engine; explicit selected installed site-packages"
        origins = json.loads(origin_report.read_text(encoding="utf-8"))
        build = json.loads(build_report.read_text(encoding="utf-8"))
        assert build["state"] == "passed"
        products = build["products"]
        report["origin_report"] = pin(origin_report)
        report["build_report"] = pin(build_report)
        cpp_pin = pin(cpp)
        cpp_build = next(p for p in products if p["path"].endswith("/imagejxx.exe"))
        assert all(cpp_pin[key] == cpp_build[key] for key in ("bytes", "sha256"))
        for value in origins["modules"].values():
            assert pin(Path(value["path"])) == value
            assert Path(value["path"]).resolve().is_relative_to(python_prefix.resolve())
        for name in ("_native", "_desktop"):
            installed_pin = origins["modules"]["imagejxx." + name]
            build_pin = next(p for p in products if Path(p["path"]).name.startswith(name + "."))
            assert all(installed_pin[key] == build_pin[key] for key in ("bytes", "sha256"))
        report["installed_modules"] = origins["modules"]
        provenance = {"schema_version": 1, "kind": "p06g.saved-roi-ui-reader-inputs.001",
                      "sources": source_rows, "contexts": {
                          "imgui_cpp": {"installed_root": str(cpp_prefix), "executable": cpp_pin,
                                        "producer_artifact": cpp_pin}}, "cases": []}
        usage = {context: {"process_seconds": 0., "producer_seconds": 0., "archive_bytes": 0}
                 for context in CONTEXTS}
        for case in CASES:
            for context in CONTEXTS:
                remaining = min(300 - (time.monotonic() - started),
                                60 - usage[context]["process_seconds"], 20)
                if remaining < 1:
                    raise TimeoutError("Paired capture parent/context budget exhausted")
                directory = output / context / case / "run"
                directory.parent.mkdir(parents=True, exist_ok=False)
                if context == "imgui_cpp":
                    argv = [str(cpp), "--saved-roi-geometry-ui-case", case, str(directory)]
                else:
                    argv = [str(python), "-I", "-X", "utf8", "-B", str(Path(__file__).absolute()),
                            "--python-case", case, "--directory", str(directory),
                            "--installed-root", str(python_prefix)]
                report["active_process"] = {"argv": argv, "case_id": case, "context": context,
                                            "timeout_seconds": int(remaining)}
                tick = time.monotonic()
                process = _run(argv, output, context + "-" + case, int(remaining))
                elapsed = time.monotonic() - tick
                process["elapsed_seconds"] = elapsed
                report["processes"].append(process)
                usage[context]["process_seconds"] += elapsed
                raw, index = directory / "case.json", directory / "case-index.json"
                row = json.loads(raw.read_text(encoding="utf-8"))
                descriptor = {"id": case, "context": context, "index": pin(index), "raw": pin(raw)}
                if context == "imgui_python":
                    origin_path = directory.parent / "launcher-origin.json"
                    origin = json.loads(origin_path.read_text(encoding="utf-8"))
                    assert origin["before"] == origin["after"]
                    assert origin["before"]["modules"] == origins["modules"]
                    assert origin["before"]["executable"] == report["python_engine"]
                    descriptor["launcher_origin"] = pin(origin_path)
                    current = {"installed_root": str(python_prefix),
                               "executable": origin["before"]["executable"],
                               "producer_artifact": origin["before"]["modules"]["imagejxx._desktop"]}
                    assert current == provenance["contexts"].setdefault(context, current)
                members = [raw, index] + [directory / name for name in row["files"]]
                archive_bytes = sum(p.stat().st_size for p in members)
                assert raw.stat().st_size <= 16 * MIB and archive_bytes <= 32 * MIB
                assert row["elapsed_seconds"] <= 15
                usage[context]["archive_bytes"] += archive_bytes
                usage[context]["producer_seconds"] += row["elapsed_seconds"]
                assert usage[context]["archive_bytes"] <= 64 * MIB
                assert usage[context]["process_seconds"] <= 60
                assert usage[context]["producer_seconds"] <= 60
                provenance["cases"].append(descriptor)
                report.pop("active_process")
                print(case, context, "observed", row["frame_count"], "frames", flush=True)
        for source in source_rows:
            assert pin(ROOT / source["logical_path"])["sha256"] == source["sha256"]
        report["usage"] = usage
        report["storage"] = dict(zip(("bytes", "files"), _storage(output)))
        write(output / "provenance.json", provenance)
        report["provenance"] = pin(output / "provenance.json")
        report["state"] = "passed"
    except BaseException as error:
        report["state"] = "failed"
        report["error"] = {"type": type(error).__name__, "message": str(error)}
        raise
    finally:
        report["elapsed_seconds"] = time.monotonic() - started
        write(output / "capture.json", report)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--python-case", choices=CASES)
    parser.add_argument("--directory", type=Path)
    parser.add_argument("--installed-root", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cpp-prefix", type=Path)
    parser.add_argument("--python-prefix", type=Path)
    parser.add_argument("--source-freeze", type=Path,
                        default=ROOT / "build/p06g-001/installed-api002/freeze.json")
    parser.add_argument("--build-report", type=Path,
                        default=ROOT / "build/p06g-001/ui-build002/run002.json")
    parser.add_argument("--origin-report", type=Path,
                        default=ROOT / "build/p06g-001/install001/origins.json")
    args = parser.parse_args()
    if args.python_case:
        if not args.directory or not args.installed_root:
            parser.error("Python case requires directory and installed-root")
        python_case(args.python_case, args.directory.absolute(), args.installed_root.absolute())
    else:
        if not args.output or not args.cpp_prefix or not args.python_prefix:
            parser.error("Capture requires output, cpp-prefix and python-prefix")
        capture(args.output, args.cpp_prefix.absolute(), args.python_prefix.absolute(),
                args.source_freeze, args.build_report, args.origin_report)


if __name__ == "__main__":
    main()
