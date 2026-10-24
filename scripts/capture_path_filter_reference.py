"""Explicit, bounded preparation/capture of pinned ImageJ path-filter observations.

Reference-only Java runs; no imagejxx imports, expected pixels or acceptance credit.
One existing reference classes cache is reused. Output directories must be fresh.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OBSERVER = ROOT / "tests/reference/ObservePathFilterTargets.java"
RANK_OBSERVER = ROOT / "tests/reference/ObservePathRankTargets.java"
TYPED_GAUSSIAN_OBSERVER = ROOT / "tests/reference/ObserveTypedPathGaussianTargets.java"
SCIENTIFIC_OBSERVER = ROOT / "tests/reference/ObserveScientificPathFilterTargets.java"
TOP_HAT_DISPLAY_OBSERVER = ROOT / "tests/reference/ObservePathTopHatDisplayTargets.java"
PINS = ROOT / "docs/parity/reference-pins.json"
RUNTIME_PINS = ROOT / "tests/fixtures/tophat-mask-imagej154r.json"
COMPOSITE = ROOT / "tests/fixtures/imagej-roi-composite-imagej154r.json"
CLASS = "ObservePathFilterTargets"
MAX_CASE_BYTES = 8 * 1024 * 1024
MAX_CAPTURE_BYTES = 32 * 1024 * 1024
MAX_STDERR_BYTES = 64 * 1024


def digest(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            result.update(chunk)
    return result.hexdigest()


def pin(path):
    path = Path(path).resolve(strict=True)
    return {"path": str(path), "bytes": path.stat().st_size, "sha256": digest(path)}


def check(value):
    actual = pin(value["path"])
    if actual != value:
        raise ValueError("Reference identity changed: " + value["path"])


def tree_files(root):
    """Inventory without following symlinks or Windows reparse points."""
    found = set()
    for parent, dirs, files in os.walk(root, followlinks=False):
        for name in dirs + files:
            path = Path(parent) / name
            stat = path.lstat()
            if path.is_symlink() or getattr(stat, "st_file_attributes", 0) & 0x400:
                raise ValueError("Linked reference member: " + str(path))
        found.update(str((Path(parent) / name).resolve()) for name in files)
    return found


def write(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def environment():
    result = os.environ.copy()
    for key in ("CLASSPATH", "JAVA_TOOL_OPTIONS", "JDK_JAVA_OPTIONS", "_JAVA_OPTIONS"):
        result.pop(key, None)
    return result


def catalog():
    original = json.loads(COMPOSITE.read_bytes())
    if original["imagej_commit"] != json.loads(PINS.read_bytes())["imagej"]["commit"]:
        raise ValueError("Composite fixture reference differs")
    rows = original["cases"]
    if [row["index"] for row in rows] != list(range(56)):
        raise ValueError("Pinned composite membership differs")
    shapes = [(f"composite-{row['index']:02}", 12, 10, row["path"]) for row in rows]
    shapes += [
        ("fractional-triangle", 23, 17, [0, 2.2, 1.2, 1, 9.2, 1.2, 1, 2.2, 8.2, 4]),
        ("curve-control-hull", 23, 17, [0, 0, 0, 1, 10, 0, 2, 15, 5, 10, 10,
                                       3, 8, 12, 2, 12, 0, 10, 4]),
        ("outside-canvas", 12, 10, [0, 30, 30, 1, 34, 30, 1, 34, 34, 1, 30, 34, 4]),
        ("consecutive-moves", 12, 10, [0, -10, -10, 0, 2, 2, 1, 6, 2, 1, 2, 6, 4]),
    ]
    result = [{"id": f"{name}-sigma{sigma}", "name": name, "width": width,
               "height": height, "commands": path, "sigma": sigma, "mode": "small-mask"}
              for name, width, height, path in shapes for sigma in (1, 12)]
    result.append({"id": "negative-billion-metadata", "name": "negative-billion-metadata",
                   "width": 80, "height": 10, "commands": [0, -1e9, 1, 1, 50, 1,
                       1, 50, 6, 1, -1e9, 6, 4], "sigma": 1, "mode": "metadata-only"})
    return result


def degenerate_catalog():
    # These distinguish one-axis-zero bounds from cleared point attachment.
    return [{"id": f"{name}-sigma{sigma}", "name": name, "width": 12, "height": 10,
             "commands": commands, "sigma": sigma, "mode": "small-mask"}
            for name, commands in [
                ("horizontal-zero-height", [0, 1, 2, 1, 6, 2, 4]),
                ("vertical-zero-width", [0, 2, 1, 1, 2, 6, 4])]
            for sigma in (1, 12)]


def rank_catalog():
    original = json.loads(COMPOSITE.read_bytes())["cases"]
    shapes = [
        ("triangle", 23, 17, [0, 2.2, 1.2, 1, 9.2, 1.2, 1, 2.2, 8.2, 4]),
        ("holes-islands", 12, 10, original[0]["path"]),
        ("off-canvas", 12, 10, original[3]["path"]),
        ("curves", 12, 10, original[2]["path"]),
    ]
    rows = []

    def add(shape, operation, pixel_type, radius, which=0, threshold=3.5,
            light=False, dont=False, inverted=False, pattern="finite"):
        name, width, height, commands = shape
        rows.append({"id": f"rank-{len(rows):03}-{operation}-{pixel_type}-{name}",
            "name": f"rank-{len(rows):03}-{operation}-{pixel_type}-{name}",
            "width": width, "height": height, "commands": commands,
            "operation": operation, "pixel_type": pixel_type, "radius": radius,
            "which_outliers": which, "threshold": threshold, "light_background": light,
            "dont_subtract": dont, "inverted_lut": inverted, "input_pattern": pattern})

    methods = ("minimum", "maximum", "mean", "variance", "median")
    for method in (*methods, "despeckle"):
        for pixel_type in ("u8", "u16", "f32"):
            for shape in shapes:
                add(shape, method, pixel_type, 1 if method == "despeckle" else 1.75)
    for radius in (0, 2.25):
        for method in methods:
            for pixel_type in ("u8", "u16", "f32"):
                add(shapes[0], method, pixel_type, radius)
    for radius in (1, 2.5):
        for method in methods:
            add(shapes[0], method, "f32", radius)
    for which in (0, 1):
        for inverted in (False, True):
            for pixel_type in ("u8", "u16", "f32"):
                for shape in shapes[:2]:
                    add(shape, "outliers", pixel_type, 2, which=which, inverted=inverted)
    for light in (False, True):
        for dont in (False, True):
            for inverted in (False, True):
                for pixel_type in ("u8", "u16", "f32"):
                    for shape in (shapes[0], shapes[1], shapes[3]):
                        add(shape, "top_hat", pixel_type, 2, light=light, dont=dont, inverted=inverted)
    for pattern in ("finite", "quiet_nan", "ieee"):
        for radius in (0, 2):
            for shape in shapes:
                add(shape, "remove_nans", "f32", radius, pattern=pattern)
    for pixel_type in ("u8", "u16", "f32"):
        add(shapes[0], "median", pixel_type, 100)
        add(shapes[0], "mean", pixel_type, 1000)
        for which in (0, 1):
            add(shapes[0], "outliers", pixel_type, 100, which=which, threshold=3.4028234663852886e38)
        for dont in (False, True):
            add(shapes[0], "top_hat", pixel_type, 1000, dont=dont)
    for pattern in ("finite", "quiet_nan", "ieee"):
        add(shapes[0], "remove_nans", "f32", 100, pattern=pattern)
    return rows


def rank_scope_catalog():
    rows = []
    for row in rank_catalog():
        if row["width"] != 23 or row["radius"] != (1 if row["operation"] == "despeckle" else
                1.75 if row["operation"] in ("minimum", "maximum", "mean", "variance", "median") else 2):
            continue
        key = (row["operation"], row["pixel_type"], row["input_pattern"])
        if any((old["operation"], old["pixel_type"], old["input_pattern"]) == key for old in rows):
            continue
        name = f"rank-scope-{len(rows):03}-{row['operation']}-{row['pixel_type']}-{row['input_pattern']}"
        rows.append(dict(row, id=name, name=name, width=12, height=10,
                         commands=[0, 0, 0, 1, 12, 0, 1, 12, 10, 1, 0, 10, 4]))
    return rows


def typed_gaussian_catalog():
    original = json.loads(COMPOSITE.read_bytes())["cases"]
    shapes = [("triangle", 23, 17, [0,2.2,1.2,1,9.2,1.2,1,2.2,8.2,4]),
              ("holes-islands", 12, 10, original[0]["path"]),
              ("curves", 12, 10, original[2]["path"]),
              ("off-canvas", 12, 10, original[3]["path"]),
              ("covers-canvas", 12, 10, [0,0,0,1,12,0,1,12,10,1,0,10,4])]
    result = []
    for pixel_type in ("u8", "u16", "f32", "rgb"):
        for shape, width, height, commands in shapes:
            for sigma_x, sigma_y in ((1,1), (12,12), (2,1), (0,0)):
                name = f"gaussian-{len(result):03}-{pixel_type}-{shape}"
                result.append({"id": name, "name": name, "width": width, "height": height,
                    "commands": commands, "pixel_type": pixel_type, "sigma_x": sigma_x,
                    "sigma_y": sigma_y, "accuracy": 0.002 if pixel_type in ("u8", "rgb") else 0.0002})
    return result


def top_hat_display_catalog():
    rows = []
    for light in (False, True):
        for dont in (False, True):
            for inverted in (False, True):
                for phase in ("preview", "apply"):
                    name = f"top-hat-display-{int(light)}{int(dont)}{int(inverted)}-{phase}"
                    rows.append({"id": name, "name": name, "width": 23, "height": 17,
                        "commands": [0,2.2,1.2,1,9.2,1.2,1,2.2,8.2,4], "phase": phase,
                        "radius": 2.0, "light_background": light, "dont_subtract": dont,
                        "inverted_lut": inverted})
    return rows


def budget_catalog(operation):
    """Wide-plane observations; same reviewed observers and exact finite grid."""
    if operation not in ("gaussian", "rank"):
        raise ValueError("Unknown budget reference operation")
    row = dict(typed_gaussian_catalog()[0] if operation == "gaussian" else rank_catalog()[0])
    name = "budget-gaussian" if operation == "gaussian" else "budget-rank-mean"
    row.update(id=name, name=name, width=256, height=128, pixel_type="f32")
    if operation == "gaussian":
        row.update(sigma_x=1, sigma_y=1, accuracy=0.0002)
    else:
        row.update(operation="mean", radius=2.5)
    return [row]


def scientific_catalog():
    p = [0,2.2,1.2,1,9.2,1.2,1,2.2,8.2,4]
    q = [0,5.2,2.2,1,12.2,2.2,1,5.2,9.2,4]
    positions = [(t,c,z) for t in (0,1) for c in (0,2) for z in (1,3)]
    rows = []
    for operation in ("gaussian", "mean"):
        for t,c,z in positions:
            name = f"scientific-{operation}-{t}{c}{z}-active"
            rows.append({"id": name, "name": name, "width": 23, "height": 17,
                         "operation": operation, "input_offset": 16*t+64*c+256*z,
                         "region": "active", "position": [t,c,z], "paths": [p]})
        name = f"scientific-{operation}-103-union"
        rows.append({"id": name, "name": name, "width": 23, "height": 17,
                     "operation": operation, "input_offset": 784,
                     "region": "union", "position": [1,0,3], "paths": [p,q]})
    return rows


def verify_inputs(archive, source, runtime):
    pins = json.loads(PINS.read_bytes())
    if digest(archive) != pins["imagej"]["sha256"]:
        raise ValueError("Source archive is not the pinned ImageJ input")
    source_files = []
    prefix = "ImageJ-" + pins["imagej"]["commit"] + "/"
    with zipfile.ZipFile(archive) as bundle:
        members = bundle.infolist()
        if len(members) > 4096 or sum(item.file_size for item in members) > 64 * 1024 * 1024:
            raise ValueError("Source archive exceeds bounded input size")
        for item in members:
            if item.is_dir():
                continue
            if not item.filename.startswith(prefix):
                raise ValueError("Unexpected source archive root")
            relative = item.filename[len(prefix):]
            path = (source / relative).resolve(strict=True)
            if not path.is_relative_to(source) or path.is_symlink():
                raise ValueError("Source member escapes declared root")
            actual = pin(path)
            if actual["sha256"] != hashlib.sha256(bundle.read(item)).hexdigest():
                raise ValueError("Source differs from pinned archive: " + relative)
            source_files.append(actual)
    runtime_files = []
    for item in json.loads(RUNTIME_PINS.read_bytes())["runtime_pins"]:
        path = (runtime / item["name"]).resolve(strict=True)
        if not path.is_relative_to(runtime):
            raise ValueError("Runtime member escapes declared root")
        actual = pin(path)
        if actual["bytes"] != item["bytes"] or actual["sha256"] != item["sha256"]:
            raise ValueError("Runtime differs from reviewed member: " + item["name"])
        runtime_files.append(actual)
    if tree_files(source) != {item["path"] for item in source_files}:
        raise ValueError("Source membership differs from pinned archive")
    if tree_files(runtime) != {item["path"] for item in runtime_files}:
        raise ValueError("Runtime membership differs from reviewed archive")
    return {"source_archive": pin(archive), "source_files": source_files,
            "runtime_files": runtime_files, "observer": pin(OBSERVER),
            "rank_observer": pin(RANK_OBSERVER),
            "typed_gaussian_observer": pin(TYPED_GAUSSIAN_OBSERVER),
            "scientific_observer": pin(SCIENTIFIC_OBSERVER),
            "top_hat_display_observer": pin(TOP_HAT_DISPLAY_OBSERVER),
            "pin_documents": [pin(PINS), pin(RUNTIME_PINS), pin(COMPOSITE)]}


def prepare(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = args.source_root.resolve(strict=True)
    runtime = args.runtime_root.resolve(strict=True)
    classes = args.classes_root.resolve()
    if classes == source or classes.is_relative_to(source) or classes == runtime or classes.is_relative_to(runtime):
        raise ValueError("Classes cache must be separate from original source/runtime")
    if classes.exists() and any(classes.iterdir()):
        raise ValueError("Prepare requires the explicitly owned classes cache to be empty")
    classes.mkdir(parents=True, exist_ok=True)
    inputs = verify_inputs(args.source_archive.resolve(strict=True), source, runtime)
    # Classpath observation needs production ij sources, not module-info or JUnit tests.
    java_sources = sorted((source / "ij").rglob("*.java")) + [OBSERVER, RANK_OBSERVER, TYPED_GAUSSIAN_OBSERVER, SCIENTIFIC_OBSERVER, TOP_HAT_DISPLAY_OBSERVER]
    if not 1 < len(java_sources) <= 1024:
        raise ValueError("Unexpected production source count")
    argfile = output / "sources.txt"
    argfile.write_text("\n".join('"' + path.as_posix() + '"' for path in java_sources) + "\n")
    javac = runtime / "bin" / ("javac.exe" if os.name == "nt" else "javac")
    argv = [str(javac), "-J-Xmx512m", "-encoding", "UTF-8", "--release", "8",
            "-d", str(classes), "@" + str(argfile)]
    receipt = {"schema": "p13i.reference.prepare.001", "state": "failed",
               "started": time.time(), "argv": argv,
               "exit_code": None, "inputs": inputs,
               "classes_root": str(classes), "classes": [], "acceptance": False}
    try:
        with (output / "compile.log").open("xb") as log:
            result = subprocess.run(argv, cwd=output, env=environment(), stdout=log,
                                    stderr=subprocess.STDOUT, timeout=120)
        receipt["exit_code"] = result.returncode
        if (output / "compile.log").stat().st_size > 16 * 1024 * 1024:
            raise ValueError("Reference compiler log exceeds output bound")
        if result.returncode:
            raise RuntimeError("Reference compilation failed; see compile.log")
        receipt["classes"] = [pin(path) for path in sorted(classes.rglob("*.class"))]
        if not (classes / (CLASS + ".class")).is_file():
            raise ValueError("Observer was not compiled")
        receipt["state"] = "prepared"
    except BaseException as error:
        receipt["error"] = {"type": type(error).__name__, "message": str(error)}
        raise
    finally:
        receipt["ended"] = time.time()
        write(output / "receipt.json", receipt)


def capture(args):
    rank_mode = args.catalog in ("rank", "rank-scopes", "budget-rank")
    typed_gaussian_mode = args.catalog in ("gaussian-typed", "budget-gaussian")
    scientific_mode = args.catalog == "scientific"
    top_hat_display_mode = args.catalog == "top-hat-display"
    prepared = json.loads(args.prepared_receipt.read_bytes())
    if prepared["schema"] != "p13i.reference.prepare.001" or prepared["state"] != "prepared":
        raise ValueError("Capture requires a successfully prepared reference")
    identities = [prepared["inputs"]["source_archive"], prepared["inputs"]["observer"],
                  *prepared["inputs"]["source_files"], *prepared["inputs"]["runtime_files"],
                  *prepared["inputs"]["pin_documents"], *prepared["classes"]]
    if "rank_observer" in prepared["inputs"]:
        identities.append(prepared["inputs"]["rank_observer"])
    if rank_mode and "rank_observer" not in prepared["inputs"]:
        raise ValueError("Rank observer is not in prepared reference inputs")
    if "typed_gaussian_observer" in prepared["inputs"]:
        identities.append(prepared["inputs"]["typed_gaussian_observer"])
    if typed_gaussian_mode and "typed_gaussian_observer" not in prepared["inputs"]:
        raise ValueError("Typed Gaussian observer is not in prepared reference inputs")
    if "scientific_observer" in prepared["inputs"]:
        identities.append(prepared["inputs"]["scientific_observer"])
    if scientific_mode and "scientific_observer" not in prepared["inputs"]:
        raise ValueError("Scientific observer is not in prepared reference inputs")
    if "top_hat_display_observer" in prepared["inputs"]:
        identities.append(prepared["inputs"]["top_hat_display_observer"])
    if top_hat_display_mode and "top_hat_display_observer" not in prepared["inputs"]:
        raise ValueError("Top Hat display observer is not in prepared reference inputs")
    for identity in identities:
        check(identity)
    if tree_files(Path(prepared["classes_root"])) != {item["path"] for item in prepared["classes"]}:
        raise ValueError("Prepared class membership changed")
    cases = (top_hat_display_catalog() if top_hat_display_mode else
             budget_catalog(args.catalog.removeprefix("budget-")) if args.catalog.startswith("budget-") else
             scientific_catalog() if scientific_mode else
             typed_gaussian_catalog() if typed_gaussian_mode else
             rank_scope_catalog() if args.catalog == "rank-scopes" else
             rank_catalog() if rank_mode else
             degenerate_catalog() if args.catalog == "degenerate" else catalog())
    if args.case and len(set(args.case)) != len(args.case):
        raise ValueError("Duplicate explicit case selection")
    selected = cases if not args.case else [row for row in cases if row["id"] in args.case]
    if not selected or (args.case and {row["id"] for row in selected} != set(args.case)):
        raise ValueError("Unknown or empty explicit case selection")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    runtime = Path(next(item["path"] for item in prepared["inputs"]["runtime_files"]
                        if Path(item["path"]).name in ("java", "java.exe")))
    receipt = {"schema": "p13i.reference.capture.001", "state": "running",
               "catalog_id": args.catalog,
               "prepared_receipt": pin(args.prepared_receipt), "driver": pin(__file__),
               "cases": [], "attempts": [], "required_cases": [row["id"] for row in cases],
               "selected_cases": [row["id"] for row in selected], "acceptance": False}
    write(output / "receipt.json", receipt)
    total = 0
    try:
        for row in selected:
            if top_hat_display_mode:
                argv = [str(runtime), "-Xms32m", "-Xmx256m", "-Djava.awt.headless=false",
                        "-cp", prepared["classes_root"], "ObservePathTopHatDisplayTargets",
                        row["name"], row["phase"], str(row["light_background"]).lower(),
                        str(row["dont_subtract"]).lower(), str(row["inverted_lut"]).lower()]
            elif scientific_mode:
                argv = [str(runtime), "-Xms32m", "-Xmx256m", "-Djava.awt.headless=true",
                        "-cp", prepared["classes_root"], "ObserveScientificPathFilterTargets",
                        row["name"], row["operation"], str(row["input_offset"]), row["region"]]
            else:
                argv = [str(runtime), "-Xms32m", "-Xmx256m", "-Djava.awt.headless=true",
                    "-cp", prepared["classes_root"],
                    "ObserveTypedPathGaussianTargets" if typed_gaussian_mode else
                    "ObservePathRankTargets" if rank_mode else CLASS,
                    row["name"], str(row["width"]), str(row["height"]),
                    ",".join(format(value, ".17g") for value in row["commands"])]
            if typed_gaussian_mode:
                argv += [row["pixel_type"], str(row["sigma_x"]), str(row["sigma_y"])]
            elif rank_mode:
                argv += [row["operation"], row["pixel_type"], str(row["radius"]),
                         str(row["which_outliers"]), str(row["threshold"]),
                         str(row["light_background"]).lower(), str(row["dont_subtract"]).lower(),
                         str(row["inverted_lut"]).lower(), row["input_pattern"]]
            elif not scientific_mode and not top_hat_display_mode:
                argv += [str(row["sigma"]), row["mode"]]
            target = output / (row["id"] + ".json")
            attempt = {"id": row["id"], "argv": argv, "started": time.time(),
                       "exit_code": None}
            receipt["attempts"].append(attempt)
            write(output / "receipt.json", receipt)
            try:
                with target.open("xb") as stdout, target.with_suffix(".stderr").open("xb") as stderr:
                    result = subprocess.run(argv, cwd=output, env=environment(), stdout=stdout,
                                            stderr=stderr, timeout=15)
                attempt["exit_code"] = result.returncode
            finally:
                attempt["ended"] = time.time()
                attempt["stdout"] = pin(target) if target.is_file() else None
                attempt["stderr"] = (pin(target.with_suffix(".stderr"))
                                     if target.with_suffix(".stderr").is_file() else None)
            total += target.stat().st_size + target.with_suffix(".stderr").stat().st_size
            if (result.returncode or target.stat().st_size > MAX_CASE_BYTES
                    or target.with_suffix(".stderr").stat().st_size > MAX_STDERR_BYTES
                    or total > MAX_CAPTURE_BYTES):
                raise ValueError("Reference process failed or exceeded output bounds: " + row["id"])
            actual = json.loads(target.read_bytes())
            if rank_mode or typed_gaussian_mode or scientific_mode or top_hat_display_mode:
                schema = ("imagejxx.reference.path-top-hat-display-target" if top_hat_display_mode else
                          "imagejxx.reference.scientific-path-filter-target" if scientific_mode else
                          "imagejxx.reference.typed-path-gaussian-target" if typed_gaussian_mode
                          else "imagejxx.reference.path-rank-target")
                valid = (actual["schema"] == schema
                         and actual["version"] == 1 and actual["name"] == row["name"]
                         and actual["imagej_version"] == "1.54r" and actual["threads"] == 1)
            else:
                valid = not (actual["schema"] != "imagejxx.reference.path-filter-target" or actual["version"] != 1
                    or actual["name"] != row["name"] or actual["mode"] != row["mode"]
                    or actual["sigma"] != row["sigma"] or actual["imagej_version"] != "1.54r"
                    or actual["threads"] != 1)
            if not valid:
                raise ValueError("Reference envelope differs from requested case")
            receipt["cases"].append({"id": row["id"], "request": row, "argv": argv,
                                      "exit_code": result.returncode, "report": pin(target)})
            write(output / "receipt.json", receipt)
        for identity in identities:
            check(identity)
        if tree_files(Path(prepared["classes_root"])) != {item["path"] for item in prepared["classes"]}:
            raise ValueError("Prepared class membership changed during capture")
        receipt["state"] = "observed"
        receipt["complete_catalog_observed"] = len(selected) == len(cases)
    except BaseException as error:
        receipt.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raise
    finally:
        write(output / "receipt.json", receipt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    prep = commands.add_parser("prepare")
    for name in ("source-archive", "source-root", "runtime-root", "classes-root", "output"):
        prep.add_argument("--" + name, type=Path, required=True)
    cap = commands.add_parser("capture")
    cap.add_argument("--prepared-receipt", type=Path, required=True)
    cap.add_argument("--output", type=Path, required=True)
    cap.add_argument("--case", action="append")
    cap.add_argument("--catalog", choices=("primary", "degenerate", "rank", "rank-scopes", "gaussian-typed", "scientific", "budget-gaussian", "budget-rank", "top-hat-display"), default="primary")
    args = parser.parse_args()
    (prepare if args.action == "prepare" else capture)(args)


if __name__ == "__main__":
    main()
