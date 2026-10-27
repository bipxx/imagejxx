"""Observe ImageJ1.54r APIs using an existing approved Windows reference cache.

No downloads, dependency builds, imagejxx imports or historical fixture updates.
Only the project-owned observer is compiled in fresh owned scratch.
"""
from __future__ import annotations
import argparse
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "62db90a269cccc1ac79f658e4602043214ed055b"
SOURCE_SHA = "09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665"
CLASS_SET_SHA = "cb466837306b5e14738d45de2f26e90640790a4699efe26fb9743662de9b18fe"
RUNTIME_SET_SHA = "7445bedc0af85664b15784ee934bcc4b87084557955409de96354b197f2d0d9f"
LIMIT = 4 * 1024**2
METHODS = ("avg", "max", "min", "sum", "sd", "median")


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def pin(path):
    path = Path(path)
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            h.update(block)
    return {"path": str(path.resolve()), "bytes": path.stat().st_size, "sha256": h.hexdigest()}


def expected_ids():
    kinds = ("u8", "u16", "f32", "rgb")
    return (
        [f"scalar.{k}.{p}.{m}" for k in kinds for p in ("full", "range_even", "singleton") for m in METHODS]
        + [f"scalar.u16.signed16.{m}" for m in METHODS]
        + [f"scalar.f32.{p}.{m}" for p in ("rounding", "nonfinite", "all_nan", "zeros_snan") for m in METHODS]
        + [f"scalar.rgb.clip.{m}" for m in METHODS]
        + [f"hyper.{k}.z{z}.all{str(a).lower()}.{m}" for k in kinds for z in (4, 1) for a in (False, True) for m in METHODS]
        + [f"hyper_rgb_c1.z{z}.all{str(a).lower()}.{m}" for z in (4, 1) for a in (False, True) for m in METHODS]
        + [f"group.{k}.{axis}.g{g}.{m}" for k in kinds for axis in ("z", "time") for g in (2, 3) for m in METHODS]
    )


def decode_rows(raw):
    if len(raw) > LIMIT:
        raise ValueError("Projection observations exceed their bounded capture")
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("Duplicate observation key")
            result[key] = value
        return result
    def nonfinite(value):
        raise ValueError("Nonfinite JSON number: " + value)
    rows = [json.loads(line, object_pairs_hook=pairs, parse_constant=nonfinite) for line in raw.splitlines() if line.strip()]
    if [r["case_id"] for r in rows] != expected_ids():
        raise ValueError("Projection observations differ from the exact ordered324-case matrix")
    for row in rows:
        rejected = row["case_id"].startswith("hyper.rgb.")
        if row["ui_executed"] is not False or row["expected_outcome"] != ("exception" if rejected else "ok"):
            raise ValueError("Reference observation scope differs")
        if rejected:
            if (row["status"] != "exception" or row.get("exception") != "java.lang.IllegalArgumentException"
                    or row.get("message") != "RGB stacks not supported"
                    or not row.get("exception_frames", [""])[0].startswith("ij.CompositeImage.")
                    or not any("ij.plugin.ZProjector.doHyperStackProjection" in frame for frame in row["exception_frames"])):
                raise ValueError("Expected ImageJ RGB-channel rejection changed")
        elif row["status"] != "ok" or row.get("output_api") is None:
            raise ValueError("Reference API failed or changed scope: " + row["case_id"])
        states = [row["before"], row["after"]] + ([] if rejected else [row["output_api"]])
        if row["group_size"]:
            states.append(row["output_after_plugin_calibration_steps"])
        for image in states:
            x, y, c, z, t = image["dimensions_xyczt"]
            if any(type(n) is not int or n <= 0 for n in (x, y, c, z, t)) or len(image["planes"]) != c*z*t:
                raise ValueError("Invalid reference image dimensions")
            word_bytes = {8: 1, 16: 2, 24: 4, 32: 4}[image["bit_depth"]]
            for plane in image["planes"]:
                if len(bytes.fromhex(plane["raw_le_hex"])) != x*y*word_bytes:
                    raise ValueError("Incomplete reference raw plane")
        if [p["raw_le_hex"] for p in row["before"]["planes"]] != [p["raw_le_hex"] for p in row["after"]["planes"]]:
            raise ValueError("ImageJ changed source pixel words: " + row["case_id"])
    return rows


def verify_inputs(base, class_receipt, runtime_receipt):
    classes = json.loads(class_receipt.read_bytes())
    runtime = json.loads(runtime_receipt.read_bytes())
    if classes["state"] != "compiled" or classes["exit_code"] != 0 or runtime["archive_sha256"] != "8e33a11c7e79eb02c055bcee40de806d16e59f0b42e58c4689bd23eb47d990bb":
        raise ValueError("Reference bootstrap identities differ")
    class_rows = [{"name": v["path"], "bytes": v["bytes"], "sha256": v["sha256"]} for v in classes["classes"] if v["path"].startswith("ij/")]
    runtime_rows = runtime["members"]
    for rows, expected in ((class_rows, CLASS_SET_SHA), (runtime_rows, RUNTIME_SET_SHA)):
        canonical = sorted(rows, key=lambda v: v["name"])
        if digest(json.dumps(canonical, sort_keys=True, separators=(",", ":")).encode()) != expected:
            raise ValueError("Reference member manifest differs from approved identity")
    actual = {p.relative_to(base/"classes").as_posix() for p in (base/"classes/ij").rglob("*.class")}
    if actual != {v["name"] for v in class_rows}:
        raise ValueError("ImageJ class membership differs")
    for directory, rows in ((base/"classes", class_rows), (base/"reviewed-jdk21", runtime_rows)):
        for item in rows:
            path = directory/item["name"]
            if path.is_symlink() or not path.is_file() or path.resolve().parent == directory.resolve().parent:
                raise ValueError("Reference member is not an owned regular input")
            observed = pin(path)
            if observed["bytes"] != item["bytes"] or observed["sha256"] != item["sha256"]:
                raise ValueError("Reference member bytes differ: " + item["name"])
    source_zip = base/"source.zip"
    if pin(source_zip)["sha256"] != SOURCE_SHA:
        raise ValueError("Wrong ImageJ source archive")
    java_files = 0
    prefix = "ImageJ-" + COMMIT + "/"
    with zipfile.ZipFile(source_zip) as archive:
        for name in archive.namelist():
            if name.startswith(prefix+"ij/") and name.endswith(".java"):
                relative = name[len(prefix):]
                if (base/"source"/relative).read_bytes() != archive.read(name):
                    raise ValueError("Extracted reference source differs: " + relative)
                java_files += 1
    return {"class_receipt": pin(class_receipt), "runtime_receipt": pin(runtime_receipt),
            "source_archive": pin(source_zip), "class_count": len(class_rows), "runtime_count": len(runtime_rows),
            "verified_java_sources": java_files, "class_set_sha256": CLASS_SET_SHA, "runtime_set_sha256": RUNTIME_SET_SHA}


def compress_checked(source, target):
    if target.exists():
        raise FileExistsError(target)
    with source.open("rb") as src, target.open("xb") as dst:
        with gzip.GzipFile(filename="", mode="wb", fileobj=dst, mtime=0, compresslevel=9) as out:
            for block in iter(lambda: src.read(65536), b""):
                out.write(block)
    with gzip.open(target, "rb") as stream:
        restored = stream.read(LIMIT+1)
    if len(restored) != source.stat().st_size or digest(restored) != pin(source)["sha256"]:
        raise ValueError("Compressed observation verification failed")
    return {"raw": pin(source), "gzip": pin(target)}


def owned_files(scratch):
    # Check ancestors too: a fresh child below a junction still follows it.
    for path in (scratch, *scratch.parents):
        info = path.lstat()
        if path.is_symlink() or getattr(info, "st_file_attributes", 0)&0x400:
            raise ValueError("Aliased reference scratch ancestor")
    files, directories = [], []
    for parent, dirs, names in os.walk(scratch, followlinks=False):
        for name in dirs+names:
            path = Path(parent)/name
            info = path.lstat()
            if path.is_symlink() or getattr(info, "st_file_attributes", 0)&0x400 or not path.resolve().is_relative_to(scratch.resolve()):
                raise ValueError("Aliased reference scratch entry")
            if stat.S_ISREG(info.st_mode): files.append(path)
            elif stat.S_ISDIR(info.st_mode): directories.append(path)
            else: raise ValueError("Nonregular reference scratch entry")
    return files, sorted(directories, key=lambda p: len(p.parts), reverse=True)


def capture(base, class_receipt, runtime_receipt, output):
    if sys.platform != "win32":
        raise ValueError("This pinned reference runtime is Windows-only")
    base = base.resolve(strict=True)
    output = output.absolute()
    if output.exists():
        raise FileExistsError(output)
    for path in output.parents:
        if path.exists() and (path.is_symlink() or getattr(path.lstat(), "st_file_attributes", 0)&0x400):
            raise ValueError("Aliased reference output ancestor")
    output.mkdir(parents=True)
    scratch = output/"scratch"
    scratch.mkdir()
    for name in ("classes", "home", "tmp", "plugins"):
        (scratch/name).mkdir()
    spec = importlib.util.spec_from_file_location("z_projection_process_owner", ROOT/"scripts/ci_owned_process.py")
    owner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(owner)
    driver = ROOT/"tests/reference/ObserveScientificZProjection.java"
    receipt = {"state": "running", "acceptance_claim": False, "imagej_commit": COMMIT,
               "scope": "324 headless API observations, including24 explicit ImageJ RGB-channel rejections; no GUI, imagejxx or full parity acceptance",
               "driver": pin(driver), "commands": []}
    def save():
        (output/"receipt.json").write_text(json.dumps(receipt, indent=2)+"\n", encoding="utf-8")
    environment = os.environ.copy()
    for name in ("CLASSPATH", "JAVA_TOOL_OPTIONS", "JDK_JAVA_OPTIONS", "_JAVA_OPTIONS", "_JAVAC_OPTIONS", "JAVAC_OPTS", "JAVA_OPTS"):
        environment.pop(name, None)
    environment.update(TEMP=str(scratch/"tmp"), TMP=str(scratch/"tmp"))
    def run(label, argv, stdout):
        with stdout.open("xb") as out, (output/(label+".stderr.log")).open("xb") as err:
            try:
                proof = owner.run_owned([str(v) for v in argv], cwd=scratch, env=environment, timeout=120,
                    stdout=out, stderr=err, receipt_path=output/(label+".process.json"))
            except owner.OwnedProcessError as error:
                receipt["commands"].append({"label": label, "proof": error.receipt})
                save()
                raise
        receipt["commands"].append({"label": label, "proof": proof})
        save()
        if proof["exit_code"] != 0 or not proof["target_completed"] or not proof["cleanup_complete"]:
            raise ValueError("Reference process failed: " + label)
    save()
    try:
        receipt["inputs"] = verify_inputs(base, class_receipt, runtime_receipt)
        jdk = base/"reviewed-jdk21/bin"
        run("compile", [jdk/"javac.exe", "-J-Xmx256m", "--release", "8", "-encoding", "UTF-8",
            "-sourcepath", "", "-classpath", base/"classes", "-d", scratch/"classes", driver], output/"compile.stdout.log")
        if not all(p.name.startswith("ObserveScientificZProjection") for p in (scratch/"classes").rglob("*.class")):
            raise ValueError("Compiler wrote dependency runtime classes instead of the observer only")
        raw = scratch/"observations.jsonl"
        run("observe", [jdk/"java.exe", "-Xms16m", "-Xmx256m", "-Djava.awt.headless=true",
            "-Duser.home="+str(scratch/"home"), "-Djava.io.tmpdir="+str(scratch/"tmp"),
            "-Dplugins.dir="+str(scratch/"plugins"), "-cp", str(scratch/"classes")+os.pathsep+str(base/"classes"),
            "ObserveScientificZProjection"], raw)
        rows = decode_rows(raw.read_bytes())
        receipt["observations"] = compress_checked(raw, output/"observations.jsonl.gz")
        receipt["case_ids"] = [r["case_id"] for r in rows]
        receipt["after_inputs"] = verify_inputs(base, class_receipt, runtime_receipt)
        if receipt["inputs"] != receipt["after_inputs"] or pin(driver) != receipt["driver"]:
            raise ValueError("Reference inputs changed during capture")
        receipt["state"] = "passed"
    except BaseException as error:
        receipt.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        raw = scratch/"observations.jsonl"
        if raw.exists() and not receipt.get("observations") and all(c["proof"].get("target_completed") and c["proof"].get("cleanup_complete") for c in receipt["commands"]):
            receipt["failure_observations"] = compress_checked(raw, output/"failed-observations.jsonl.gz")
        raise
    finally:
        save()
        if all(c["proof"].get("target_completed") and c["proof"].get("cleanup_complete") for c in receipt["commands"]):
            raw = scratch/"observations.jsonl"
            if raw.exists() and not receipt.get("observations") and not receipt.get("failure_observations"):
                receipt["scratch_retirement"] = "retained_unverified_observation_capture"
                save()
                raise ValueError("Preserving raw capture because retention did not verify")
            files, directories = owned_files(scratch)
            for path in files:
                receipt.setdefault("scratch_identities", []).append(pin(path))
            diagnostics = [p for p in files if p != raw and p.suffix != ".class"]
            if diagnostics:
                archive = output/"scratch-diagnostics.zip"
                with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as z:
                    for path in diagnostics: z.write(path, path.relative_to(scratch).as_posix())
                with zipfile.ZipFile(archive) as z:
                    for path in diagnostics:
                        restored = z.read(path.relative_to(scratch).as_posix())
                        if len(restored) != path.stat().st_size or digest(restored) != pin(path)["sha256"]:
                            raise ValueError("Reference failure diagnostics did not verify")
                receipt["scratch_diagnostics"] = pin(archive)
            save()
            for path in files:
                identity = next(v for v in receipt["scratch_identities"] if v["path"] == str(path.resolve()))
                if pin(path) != identity: raise ValueError("Reference scratch changed before retirement")
                path.unlink()
            for path in directories: path.rmdir()
            scratch.rmdir()
            receipt["scratch_retirement"] = "retired_after_terminal_processes_and_verified_capture"
            save()
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-root", type=Path, required=True)
    parser.add_argument("--class-receipt", type=Path, required=True)
    parser.add_argument("--runtime-receipt", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = capture(args.reference_root, args.class_receipt.resolve(strict=True), args.runtime_receipt.resolve(strict=True), args.output)
    print(json.dumps({"state": result["state"], "cases": len(result["case_ids"]), "capture": result["observations"]["gzip"]}))


if __name__ == "__main__":
    main()
