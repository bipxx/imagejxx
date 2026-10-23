"""Capture combined ImageJ transitions without importing imagejxx or patching ImageJ."""
import argparse
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import subprocess

DRIVERS = ("RgbHistogramAutoReference.java", "RgbContrastSequenceReference.java",
           "RgbStackApplyReference.java", "RgbContrastNavigationReference.java",
           "RgbContrastCombinedReference.java")
UPSTREAM = ("ij/ImagePlus", "ij/ImageStack", "ij/plugin/frame/ContrastAdjuster",
            "ij/process/ColorProcessor", "ij/process/ColorStatistics",
            "ij/process/ImageStatistics", "ij/process/ByteProcessor",
            "ij/gui/Roi", "ij/gui/Line", "ij/gui/PointRoi", "ij/gui/PolygonRoi", "ij/gui/OvalRoi")
IDS = ("axis", "origin", "first_channels", "second_channels", "current_channels",
       "roi", "changed_roi", "other_roi", "automatic")
NUMBERS = ("minimum", "maximum", "processor_minimum", "processor_maximum")
HASHES = ("first_a", "selected_a", "mapped_b", "return_draft_a", "return_reset_a",
          "final_a", "preview_stack", "applied_stack")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(raw):
    rows = []
    for line in raw.splitlines():
        fields = line.split()
        if len(fields) != 22:
            raise ValueError(f"Unexpected reference row: {line!r}")
        row = dict(zip(IDS, map(int, fields[:9])))
        row.update(zip(NUMBERS, map(float, fields[9:13])))
        row["auto_threshold"] = int(fields[13])
        for name, value in zip(HASHES, fields[14:]):
            if len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
                raise ValueError("Invalid pixel SHA-256")
            row[name+"_sha256"] = value
        if row["second_channels"] == row["first_channels"] or row["current_channels"] != row["second_channels"] % 7+1:
            raise ValueError("Unexpected promotion order")
        if row["changed_roi"] != (row["roi"]+1) % 9 or row["other_roi"] != (row["roi"]+2) % 9:
            raise ValueError("Unexpected selection order")
        if not all(math.isfinite(row[n]) for n in NUMBERS) or row["minimum"] > row["maximum"]:
            raise ValueError("Invalid final range")
        if row["auto_threshold"] not in (0, 5000) or not row["automatic"] and row["auto_threshold"]:
            raise ValueError("Unexpected Auto cycle")
        if row["return_reset_a_sha256"] != row["selected_a_sha256"]:
            raise ValueError("Returning Reset lost the promoted baseline")
        rows.append(row)
    expected = [(axis, origin, first, second, roi, automatic)
                for axis, origin, first, second, roi, automatic in itertools.product(range(3), range(2), range(1, 8), range(1, 8), range(9), range(2)) if first != second]
    if [(r["axis"], r["origin"], r["first_channels"], r["second_channels"], r["roi"], r["automatic"]) for r in rows] != expected:
        raise ValueError("Missing, duplicated or reordered cases")
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--imagej-root", type=Path, required=True)
    parser.add_argument("--jdk", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    args = parser.parse_args()
    root, jdk, output = args.imagej_root.resolve(), args.jdk.resolve(), args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    classes = output/"classes"
    classes.mkdir()
    suffix = ".exe" if (jdk/"bin/java.exe").exists() else ""
    java, javac = (jdk/f"bin/{n}{suffix}" for n in ("java", "javac"))
    source = Path(__file__).resolve().parent
    commands = [[str(javac), "-encoding", "UTF-8", "-cp", str(root/"classes"), "-d", str(classes),
                 *[str(source/n) for n in DRIVERS]],
                [str(java), "-Djava.awt.headless=false", "-cp", os.pathsep.join(map(str, (classes, root/"classes"))),
                 "ij.plugin.frame.RgbContrastCombinedReference"]]
    for name, command in zip(("compile", "reference"), commands):
        (output/f"{name}.command.json").write_text(json.dumps(command, indent=2)+"\n", encoding="utf-8")
        result = subprocess.run(command, capture_output=True, timeout=180)
        (output/f"{name}.stdout").write_bytes(result.stdout)
        (output/f"{name}.stderr").write_bytes(result.stderr)
        result.check_returncode()
    rows = extract((output/"reference.stdout").read_text(encoding="utf-8"))
    pins = {"drivers/"+n: digest(source/n) for n in DRIVERS}
    for n in UPSTREAM:
        pins["sources/"+n+".java"] = digest(root/"sources"/(n+".java"))
        pins["classes/"+n+".class"] = digest(root/"classes"/(n+".class"))
    pins.update({"jdk/java": digest(java), "jdk/javac": digest(javac)})
    pins.update({"compiled_drivers/"+p.relative_to(classes).as_posix(): digest(p)
                 for p in sorted(classes.rglob("*.class"))})
    fixture = dict(schema="imagejxx.reference.rgb-combined", schema_version=1, imagej_version="1.54r",
                   driver="Actual component itemStateChanged callback plus inherited Reset/Auto, setPosition/setup and private applyRGBStack; presentation bypassed; no AWT queue dispatch",
                   promotion_pairs=42, shape_tczyxs=[2, 2, 2, 8, 256, 3], weights=[1/3, 1/3, 1/3],
                   source_formula=["(x+11*c+23*z+41*t)&255", "(73*x+19*y+17*c+31*z+47*t)&255", "(255-x+53*c+59*z+61*t)&255"],
                   selection_order=["none", "rectangle", "irregular_mask", "empty_mask", "line", "points", "polyline", "angle", "oval"],
                   ranges=dict(first=[10.9, 40.1], selected=[20.25, 200.75], other=[-30.25, 400.5], replaced_return_draft=[5.25, 90.75], final=[-10.2, 160.9]),
                   reference_stdout_sha256=digest(output/"reference.stdout"), pins=pins, cases=rows)
    with args.fixture.open("x", encoding="utf-8") as f:
        f.write(json.dumps(fixture, indent=2)+"\n")
    print(f"Captured {len(rows)} combined component/navigation/manual-Auto/final-stack cases")


if __name__ == "__main__":
    main()
