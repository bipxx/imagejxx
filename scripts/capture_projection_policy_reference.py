"""Capture252 density/channel policy cases with the unchanged approved ImageJ runtime.
Only two project-owned observers are compiled. No download, runtime build,
imagejxx execution or historical corpus mutation. Fresh scratch is retired after
terminal processes and verified lossless capture, including failure diagnostics.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("rgb_reference_shared", ROOT/"scripts/capture_z_projection_reference.py")
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
pin, digest, verify_inputs = shared.pin, shared.digest, shared.verify_inputs
compress_checked, owned_files = shared.compress_checked, shared.owned_files
COMMIT, LIMIT, METHODS = shared.COMMIT, shared.LIMIT, shared.METHODS


def expected_ids():
    return ([f"policy.channels.{k}.none.{r}.{m}" for k in ("u8","u16","f32","rgb")
             for r in ("full","even","g2","g3") for m in METHODS]
            + [f"policy.density.{k}.{p}.{r}.{m}" for k in ("u8","u16")
               for p in ("linear","poly2","zero_clip","custom_signed","custom_range","custom_ieee")
               for r in ("full","even") for m in METHODS]
            + [f"policy.density.u8.od.{r}.{m}" for r in ("full","even") for m in METHODS])


def decode_rows(raw):
    if not raw or len(raw)>LIMIT:raise ValueError("Policy capture byte bound differs")
    rows=[json.loads(line) for line in raw.splitlines()]
    if [r["case_id"] for r in rows]!=expected_ids():raise ValueError("Policy case membership differs")
    for row in rows:
        if row["ui_executed"] is not False or row["status"]!="ok":raise ValueError("Policy scope/result differs")
        if row["table_entries_used"]!=row["after_table_entries_used"]:raise ValueError("Source calibration table changed")
        if [p["raw_le_hex"] for p in row["before"]["planes"]]!=[p["raw_le_hex"] for p in row["after"]["planes"]]:raise ValueError("Source pixels changed")
        for key in ("before","after","output_api","output_after_plugin_calibration_steps"):
            if key not in row:continue
            image=row[key];x,y,c,z,t=image["dimensions_xyczt"]
            if any(type(v) is not int or v<=0 for v in (x,y,c,z,t)) or len(image["planes"])!=c*z*t:raise ValueError("Invalid policy geometry")
            size={8:1,16:2,24:4,32:4}[image["bit_depth"]]
            if any(len(bytes.fromhex(p["raw_le_hex"]))!=x*y*size for p in image["planes"]):raise ValueError("Invalid policy plane bytes")
    return rows



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
    driver = ROOT/"tests/reference/ObserveProjectionPolicy.java"
    helper = ROOT/"tests/reference/ObserveScientificZProjection.java"
    source_identities = [pin(driver), pin(helper), pin(Path(__file__)), pin(ROOT/"scripts/capture_z_projection_reference.py")]
    receipt = {"state": "running", "acceptance_claim": False, "imagej_commit": COMMIT,
               "scope": "252 headless density/channel API observations; no GUI, imagejxx or full parity acceptance",
               "driver": pin(driver), "source_identities": source_identities, "temporary_reserve_bytes": 32*1024**2, "reserve_reason": "One bounded4MiB raw capture and observer classes; existing JDK/ImageJ unchanged; scratch immediately retired after verified compressed retention", "commands": []}
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
            "-sourcepath", "", "-classpath", base/"classes", "-d", scratch/"classes", helper, driver], output/"compile.stdout.log")
        if not all(p.name.startswith(("ObserveScientificZProjection", "ObserveProjectionPolicy")) for p in (scratch/"classes").rglob("*.class")):
            raise ValueError("Compiler wrote dependency runtime classes instead of the observer only")
        raw = scratch/"observations.jsonl"
        run("observe", [jdk/"java.exe", "-Xms16m", "-Xmx256m", "-Djava.awt.headless=true",
            "-Duser.home="+str(scratch/"home"), "-Djava.io.tmpdir="+str(scratch/"tmp"),
            "-Dplugins.dir="+str(scratch/"plugins"), "-cp", str(scratch/"classes")+os.pathsep+str(base/"classes"),
            "ObserveProjectionPolicy"], raw)
        rows = decode_rows(raw.read_bytes())
        receipt["observations"] = compress_checked(raw, output/"observations.jsonl.gz")
        receipt["case_ids"] = [r["case_id"] for r in rows]
        receipt["after_inputs"] = verify_inputs(base, class_receipt, runtime_receipt)
        if receipt["inputs"] != receipt["after_inputs"] or [pin(driver), pin(helper), pin(Path(__file__)), pin(ROOT/"scripts/capture_z_projection_reference.py")] != source_identities:
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
