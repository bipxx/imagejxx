"""Bounded C01..C12 captures of ordinary installed desktop launchers.

No product import, private UI case, editor method, acceptance oracle, or shell
launch is used. Native V3 fixture bytes are owned stdlib wire literals. Each
normal executable / python -m imagejxx.ui process has its own Windows Job,
captured argv/cwd/environment/exit/stdout/stderr, and immutable file snapshots.
Independent test_startup_launchers.py reads the resulting observations.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import time
import types
import zlib

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
MIB = 1024 * 1024
CASES = tuple(f"C{i:02}" for i in range(1, 13))
CONTEXTS = ("cpp", "python")
CONTRACT = {"path": str(ROOT / "build/p06j-001/entry-adoption001/contract.json"),
            "bytes": 23982, "sha256": "5a22fb7872a9e505173d620657f5a66f3aa97d4d03c29d8677938cd94a27a014"}
SOURCE_ROLES = {
    "launcher_capture": "scripts/capture_startup_launchers.py",
    "capture_supervisor": "scripts/capture_shortcuts_history_ui.py",
    "process_helper": "scripts/capture_windows_platform_workload.py",
    "reader": "tests/python/test_startup_launchers.py",
    "reader_primitives": "tests/python/startup_observation_reader.py",
    "window": "src/desktop/window.cpp", "window_header": "src/desktop/window.hpp",
    "desktop_main": "src/desktop/main.cpp", "desktop_binding": "src/desktop/python.cpp",
    "python_package": "src/imagejxx/__init__.py", "python_wrapper": "src/imagejxx/ui/app.py",
    "python_entry": "src/imagejxx/ui/__main__.py", "python_ui_init": "src/imagejxx/ui/__init__.py",
    "python_preferences": "src/imagejxx/ui/persistence.py",
    "startup_core": "src/native/startup.cpp", "startup_header": "include/imagejxx/startup.hpp",
    "native_preferences": "src/native/preferences.cpp", "native_settings": "src/native/settings.cpp",
    "session": "src/native/session.cpp", "application": "src/native/application.cpp",
    "runtime_observer": "src/desktop/startup_ui_probe.cpp",
    "runtime_observer_header": "src/desktop/startup_ui_probe.hpp",
}


def load_source(name, path):
    module = types.ModuleType(name)
    module.__file__ = str(path)
    exec(compile(path.read_bytes(), str(path), "exec", dont_inherit=True), module.__dict__)
    return module


SUPERVISOR = load_source("startup_launcher_supervisor", ROOT / SOURCE_ROLES["capture_supervisor"])
PROCESS = load_source("startup_launcher_process", ROOT / SOURCE_ROLES["process_helper"])
require = SUPERVISOR.require
pin = SUPERVISOR.pin
verify = SUPERVISOR.verify
read_json = SUPERVISOR.read_json
write = SUPERVISOR.write


def encode(value):
    return (json.dumps(value, ensure_ascii=False, allow_nan=False, indent=2) + "\n").encode("utf-8")


def selection(kind, points):
    return {"kind": kind, "points": points, "width": 1.0, "attributes": {}}


def literal_document(scientific):
    shape = [2, 3, 2, 6, 8, 1] if scientific else [1, 1, 2, 6, 8, 1]
    planes = []
    for t in range(shape[0]):
        for c in range(shape[1]):
            for z in range(2):
                values = [40*t + 30*c + 5*z + 2*y + x if scientific else 7*z + 3*y + x
                          for y in range(6) for x in range(8)]
                planes.append(base64.b64encode(struct.pack("<48d", *values)).decode("ascii"))
    return {"axes": "TCZYXS", "shape": shape, "pixel_type": "uint16" if scientific else "uint8",
            "sample_policy": "finite_only", "storage": "Float64LE", "planes": planes,
            "calibration": {"spacing": [1.25, .5, 2.5], "origin": [-2., 3., 4.],
                            "direction": [1., 0., 0., 0., 1., 0., 0., 0., 1.], "unit": "um",
                            "frame_interval": .75, "time_origin": 2., "time_unit": "s"},
            "metadata": {"title": "Owned U16 scientific" if scientific else "Owned U8 stack",
                         "properties": {"original_path": "deleted-owned-original.pgm"},
                         "extensions": {"vendor": {"opaque": [True, None, "retained"]}}, "record_layout": {}},
            "display": {"t": 1 if scientific else 0, "c": 2 if scientific else 0,
                        "crosshair": [3, 2, 1], "orthogonal": scientific, "linked_zoom": False,
                        "zoom": 2.25, "plane_zoom": [1.5, 2.5, 3.5],
                        "pan": [[.25, -.5], [1.25, -1.5], [2.25, -2.5]], "properties": {}},
            "selection": selection("rectangle", [[1, 1], [5, 4]]),
            "previous_selection": selection("line", [[0, 0], [7, 5]]),
            "rois": [{"name": "positioned", "selection": selection("rectangle", [[2, 1], [6, 5]]),
                      "t": 1 if scientific else 0, "c": 2 if scientific else 0, "z": 1,
                      "attributes": {"inert": True}}],
            "overlays": [{"type": "text", "text": "owned inert overlay"}], "fourier": None}


def workspace_bytes(changed=False):
    value = {"format": "imagejxx-session", "version": 3,
             "documents": [literal_document(False), literal_document(True)],
             "results": [{"Value": 17}],
             "workflow": [{"operation": "must_never_execute", "historical_id": 777}],
             "extensions": {"imagejxx_workspace": {"version": 1, "active_index": 0},
                            "vendor": {"retained": True}}}
    if changed:
        values = list(struct.unpack("<48d", base64.b64decode(value["documents"][1]["planes"][11])))
        require(values[19] == 112, "Owned changed-wire index differs")
        values[19] = 113
        value["documents"][1]["planes"][11] = base64.b64encode(struct.pack("<48d", *values)).decode("ascii")
    payload = encode(value)
    result = b"IMAGEJXX3\n" + struct.pack("<Q", len(payload)) + zlib.compress(payload)
    require(len(result) <= 4 * MIB, "Owned V3 fixture bound")
    return result


def snapshot(path, destination):
    value = {"source_path": str(path), "exists": path.exists()}
    if not value["exists"]:
        value.update(byte_count=0, hex="", snapshot=None)
        return value
    raw = path.read_bytes()
    require(len(raw) <= 4 * MIB, "Owned fixture/config snapshot exceeds4MiB")
    with destination.open("xb") as stream:
        stream.write(raw)
    value.update(byte_count=len(raw), hex=raw.hex(), snapshot=pin(destination, allow_empty=True))
    return value


def fixtures(case, directory):
    root = directory / "fixture"
    cwd, config = root / "launch", root / "\u914d\u7f6e \u03b1"
    cwd.mkdir(parents=True); config.mkdir()
    preferences, workspace = config / "preferences.json", root / "workspace.ijx"
    first, third, missing = cwd / "first.pgm", cwd / "third.pgm", cwd / "missing.pgm"
    for path, offset in ((first, 3), (third, 9)):
        with path.open("xb") as stream:
            stream.write(b"P5\n8 6\n255\n" + bytes(offset + y + x for y in range(6) for x in range(8)))
    original = workspace_bytes()
    with workspace.open("xb") as stream:
        stream.write(original)
    preimage = root / "workspace-original.ijx"
    with preimage.open("xb") as stream:
        stream.write(original)
    values = {"history_limit": 3, "history_memory_mb": 1,
              "shortcut_bindings": ["analysis.measure=Ctrl+K"]}
    if case != "C01":
        values.update(startup_mode="workspace", startup_workspace="../workspace.ijx")
    if case in ("C03", "C04", "C05", "C08", "C09"):
        values["startup_workspace"] = str(root / "missing-configured.ijx")
    if case == "C10":
        values["startup_mode"] = "bad-known-mode"
    if case == "C11":
        # Keep the same named file and retain its original bytes separately.
        workspace.write_bytes(workspace_bytes(changed=True))
    payload = {"format": "imagejxx-preferences", "version": 1, "values": values,
               "extras": {"recent": ["must-not-open.pgm"], "layout": "[Window][Preferences]\nPos=60,70\nSize=700,500\n",
                          "vendor": {"opaque": [True, None, 17]}}, "vendor_root": {"retained": True}}
    preferences.write_bytes(encode(payload))
    arguments = ["--preferences", str(preferences)]
    if case == "C03": arguments += [str(first)]
    elif case == "C04": arguments += ["--workspace", os.path.relpath(workspace, cwd)]
    elif case == "C05": arguments += ["--no-restore-workspace"]
    elif case == "C06": arguments += ["--workspace", str(workspace), str(first)]
    elif case == "C07": arguments += ["--workspace", str(workspace), "--no-restore-workspace"]
    elif case == "C08": arguments += ["first.pgm", "./first.pgm"]
    elif case == "C09": arguments += [str(first), str(missing), str(third)]
    elif case == "C12": arguments += ["--reset-layout"]
    files = {"preferences": preferences, "workspace": workspace, "workspace_original": preimage,
             "first": first, "third": third, "missing": missing,
             "configured_missing": root / "missing-configured.ijx"}
    return cwd, arguments, files


def run_process(api, argv, cwd, environment, directory, deadline, process_rows, output_root):
    start = time.monotonic()
    remaining = min(15., deadline - start)
    require(remaining > 0, "Launcher aggregate deadline exhausted")
    row = {"argv": argv, "cwd": str(cwd), "timeout_seconds": remaining, "state": "starting",
           "environment": {key: environment[key] for key in ("PYTHONPATH", "PYTHONNOUSERSITE", "PYTHONDONTWRITEBYTECODE", "PYTHONPYCACHEPREFIX")
                           if key in environment},
           "removed_environment": ["PYTHONHOME", "IMAGEJXX_CONFIG_DIR", "IMAGEJXX_PREFERENCES"]}
    process_rows.append(row)
    process = None
    try:
        with (directory / "stdout.bin").open("xb") as stdout, (directory / "stderr.bin").open("xb") as stderr:
            process = PROCESS.JobProcess(api, argv, cwd, environment, stdout, stderr)
            row.update(pid=process.pid, creation_filetime=process.creation, state="running")
            while True:
                SUPERVISOR.storage(directory, {"stage_bytes": 32 * MIB})
                SUPERVISOR.storage(output_root, {"stage_bytes": 64 * MIB})
                require(time.monotonic() - start <= remaining, "Normal launcher exceeded bounded timeout")
                code = process.exit_code()
                if code != 259:
                    row.update(exit_code=code, state="exited")
                    break
                time.sleep(.025)
    except BaseException as error:
        row.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        if process is not None:
            until = time.monotonic() + 10
            try:
                row["owned_job_pids_before_cleanup"] = sorted(process.pids())
                if process.pids():
                    require(api.kernel.TerminateJobObject(process.job, 1), "Cannot terminate owned launcher Job")
                while process.pids():
                    require(time.monotonic() <= until, "Owned launcher cleanup exceeds10s")
                    time.sleep(.01)
                row["owned_job_pids_after_cleanup"] = sorted(process.pids())
            finally:
                process.close()
        row["elapsed_seconds"] = time.monotonic() - start
        row["stdout"] = pin(directory / "stdout.bin", allow_empty=True)
        row["stderr"] = pin(directory / "stderr.bin", allow_empty=True)
    return row


def capture(args):
    require(os.name == "nt", "Normal launcher capture requires Windows Job containment")
    output = args.output.absolute().resolve()
    build_root = (ROOT / "build").resolve()
    require(output != build_root and output.is_relative_to(build_root), "Fresh output must be an owned workspace build child")
    output.mkdir(exist_ok=False)
    sys.pycache_prefix = str(output / "unused-bytecode-cache")
    started, deadline = time.monotonic(), time.monotonic() + 120
    captured = {"schema_version": 1, "kind": "p06j.normal-launcher-capture.001", "state": "running",
                "acceptance_claim": False, "processes": [], "cases": [],
                "limits": {"cases": 24, "frames_per_process": 3, "seconds_per_process": 15,
                           "seconds_parent": 120, "raw_per_case": 16*MIB, "raw_total": 32*MIB,
                           "archive_total": 64*MIB}}
    consumed = []
    try:
        bindings = read_json(args.product_bindings)
        require(bindings["schema_version"] == 1 and bindings["kind"] == "p06j.installed-ui-capture-bindings.001" and
                bindings["kind_selection"] == "startup-workspace", "Wrong explicit P06j bindings")
        require(bindings["adopted_contract"] == CONTRACT, "Wrong adopted startup contract")
        consumed.append(verify(bindings["adopted_contract"]))
        require(verify(bindings["source_freeze"]) == pin(args.source_freeze) and
                verify(bindings["build_receipt"]) == pin(args.build_receipt), "Explicit build/source input differs")
        build, frozen_document = read_json(args.build_receipt), read_json(args.source_freeze)
        require(build["state"] == "passed" and build["source_freeze"] == bindings["source_freeze"] and
                build["built_products"] == bindings["built_products"], "Build does not bind these products/sources")
        frozen = {row["logical_path"]: row for row in frozen_document["sources"]}
        require(len(frozen) == len(frozen_document["sources"]), "Duplicate frozen source")
        sources = []
        for role, logical in sorted(SOURCE_ROLES.items()):
            require(logical in frozen, "Required launcher source absent from freeze: " + logical)
            observed = verify(frozen[logical]); active = pin(ROOT / logical)
            require((active["bytes"], active["sha256"]) == (observed["bytes"], observed["sha256"]), "Active source differs from freeze")
            destination = output / "source" / logical
            destination.parent.mkdir(parents=True, exist_ok=True)
            with Path(observed["path"]).open("rb") as source, destination.open("xb") as target:
                target.write(source.read(observed["bytes"] + 1))
            copied = pin(destination)
            require((copied["bytes"], copied["sha256"]) == (observed["bytes"], observed["sha256"]), "Source copy differs")
            sources.append({"role": role, "logical_path": logical, **copied})
            consumed.extend((observed, active))
        canonical = [{key: row[key] for key in ("role", "logical_path", "bytes", "sha256")}
                     for row in sorted(sources, key=lambda item: item["role"])]
        source_set = hashlib.sha256(json.dumps(canonical, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        products = {"cpp": bindings["products"]["imgui_cpp"], "python": bindings["products"]["imgui_python"]}
        require(set(bindings["built_products"]) == {"cpp", "_native", "_desktop"}, "Built product membership differs")
        for product in bindings["built_products"].values(): consumed.append(verify(product))
        for context in CONTEXTS:
            for value in products[context].values(): consumed.append(verify(value))
        python = verify(bindings["python_engine"])
        require(python == pin(SUPERVISOR.engine()), "Capture must use the explicit actual Python engine")
        require(products["python"]["executable"] == python, "Python executable product differs")
        site = args.python_site_packages.resolve(strict=True)
        numpy_site = args.numpy_site_packages.resolve(strict=True)
        require(Path(bindings["numpy"]["site_packages"]).resolve() == numpy_site, "NumPy site differs")
        for name, value in bindings["installed_modules"].items():
            observed = verify(value); consumed.append(observed)
            require(Path(observed["path"]).is_relative_to(site), "Installed module escapes selected site")
        consumed.append(verify(bindings["numpy"]["module"]))
        cpp_prefix = args.cpp_prefix.resolve(strict=True)
        require(Path(products["cpp"]["executable"]["path"]).is_relative_to(cpp_prefix), "C++ executable escapes selected installation")
        for selected, built in ((products["cpp"]["executable"], bindings["built_products"]["cpp"]),
                                (bindings["installed_modules"]["imagejxx._native"], bindings["built_products"]["_native"]),
                                (bindings["installed_modules"]["imagejxx._desktop"], bindings["built_products"]["_desktop"])):
            require((selected["bytes"], selected["sha256"]) == (built["bytes"], built["sha256"]), "Installed compiled product differs from build")
        api = PROCESS.Win32(None)
        provenance = {"schema_version": 1, "kind": "p06j.normal-launcher-reader-inputs.001", "sources": sources,
                      "source_set_sha256": source_set, "adopted_contract": bindings["adopted_contract"],
                      "contexts": {}, "cases": [], "installed_modules": bindings["installed_modules"],
                      "numpy": bindings["numpy"]}
        raw_total = 0
        for case in CASES:
            for context in CONTEXTS:
                require(time.monotonic() < deadline, "Launcher aggregate deadline exhausted")
                directory = output / context / case
                directory.mkdir(parents=True, exist_ok=False)
                cwd, arguments, files = fixtures(case, directory)
                archives = directory / "file-snapshots"
                archives.mkdir()
                before = {role: snapshot(path, archives / ("before-" + role + ".bin")) for role, path in files.items()}
                observation = directory / "normal-startup.json"
                normal = ["--hidden", "--frames", "3", "--startup-observe", str(observation), *arguments]
                argv = ([products["cpp"]["executable"]["path"], *normal] if context == "cpp" else
                        [python["path"], "-X", "utf8", "-B", "-m", "imagejxx.ui", *normal])
                environment = os.environ.copy()
                for name in ("PYTHONHOME", "PYTHONPATH", "PYTHONPYCACHEPREFIX", "IMAGEJXX_CONFIG_DIR", "IMAGEJXX_PREFERENCES"):
                    environment.pop(name, None)
                environment.update(PYTHONNOUSERSITE="1", PYTHONDONTWRITEBYTECODE="1")
                if context == "python":
                    environment["PYTHONPATH"] = os.pathsep.join((str(site), str(numpy_site)))
                    environment["PYTHONPYCACHEPREFIX"] = str(directory / "unused-bytecode-cache")
                process = run_process(api, argv, cwd, environment, directory, deadline, captured["processes"], output)
                after = {role: snapshot(path, archives / ("after-" + role + ".bin")) for role, path in files.items()}
                observed = pin(observation) if observation.exists() else None
                if observed: require(observed["bytes"] <= 16 * MIB, "Normal observation bound")
                raw = {"schema": "imagejxx.startup-normal-launcher-observations", "version": 1,
                       "contract": "p06j.saved-workspace-startup.001", "case_id": case, "context": context,
                       "observation_state": "observed", "acceptance_claim": False,
                       "input_kind": "normal_os_launcher", "process": process,
                       "fixture_setup": {"input_kind": "stdlib_owned_wire_literal", "product_imported": False,
                                         "formula_u8": "7*z+3*y+x", "formula_u16": "40*t+30*c+5*z+2*y+x"},
                       "files_before": before, "files_after": after, "observation": observed}
                raw_path = directory / "case.json"
                write(raw_path, raw)
                descriptor = {"id": case, "context": context, "raw": pin(raw_path)}
                provenance["cases"].append(descriptor); captured["cases"].append(descriptor)
                raw_total += descriptor["raw"]["bytes"] + (observed["bytes"] if observed else 0)
                require(raw_total <= 32*MIB, "Launcher raw aggregate bound")
                SUPERVISOR.storage(output, {"stage_bytes": 64*MIB})
        receipt = output / "source-product-binding.json"
        write(receipt, {"schema_version": 1, "kind": "p06j.normal-launcher-source-product-binding.001",
                        "source_set_sha256": source_set, "sources": sources, "products": products,
                        "passed_build_receipt": pin(args.build_receipt), "source_freeze": pin(args.source_freeze),
                        "explicit_product_bindings": pin(args.product_bindings), "acceptance_claim": False})
        for context in CONTEXTS:
            provenance["contexts"][context] = {"installed_root": str(cpp_prefix if context == "cpp" else site),
                                                **products[context], "build_receipt": pin(receipt),
                                                "source_set_sha256": source_set}
        write(output / "provenance.json", provenance)
        for value in consumed: verify(value)
        require(time.monotonic() <= deadline, "Launcher aggregate deadline exceeded")
        captured.update(state="observed", provenance=pin(output / "provenance.json"), paired_raw_bytes=raw_total)
    except BaseException as error:
        captured.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        captured["elapsed_seconds"] = time.monotonic() - started
        write(output / "capture.json", captured)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("output", "cpp-prefix", "python-site-packages", "numpy-site-packages",
                 "build-receipt", "source-freeze", "product-bindings"):
        parser.add_argument("--" + name, type=Path, required=True)
    capture(parser.parse_args())


if __name__ == "__main__":
    main()
