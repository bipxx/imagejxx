"""Capture real ImageJ RGB navigation semantics; never load imagejxx modules.

Requires the unmodified ImageJ 1.54r source/classes and a JDK. Outputs all raw
compiler/runtime evidence into a new, caller-owned directory. Existing fixture
replacement requires an explicit --fixture path; no build directories are deleted.
"""
from __future__ import annotations

import argparse
import hashlib
import itertools
import json
from pathlib import Path
import subprocess


DRIVERS = (
    "RgbHistogramAutoReference.java",
    "RgbContrastSequenceReference.java",
    "RgbStackApplyReference.java",
    "RgbContrastNavigationReference.java",
)
UPSTREAM = (
    "ij/ImagePlus",
    "ij/ImageStack",
    "ij/plugin/frame/ContrastAdjuster",
    "ij/process/ColorProcessor",
)
HASH_FIELDS = (
    "original_a", "original_b", "first_map_a", "changed_selection_map_a",
    "mapped_b", "return_reset_a", "return_remap_a", "remapped_stack",
    "reset_stack",
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(text: str) -> list[dict]:
    rows = []
    expected = list(itertools.product(range(3), range(2), range(1, 8), range(9)))
    for line in text.splitlines():
        fields = line.split()
        if len(fields) != 14:
            raise ValueError(f"Unexpected reference row: {line!r}")
        axis, origin, channels, roi, changed_roi = map(int, fields[:5])
        if changed_roi != (roi + 1) % 9:
            raise ValueError("Unexpected selection transition")
        for value in fields[5:]:
            if len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
                raise ValueError("Invalid pixel SHA-256")
        row = dict(zip(("axis", "origin", "channels", "roi", "changed_roi"),
                       (axis, origin, channels, roi, changed_roi)))
        row.update({key + "_sha256": value for key, value in zip(HASH_FIELDS, fields[5:])})
        if row["return_reset_a_sha256"] != row["changed_selection_map_a_sha256"]:
            raise ValueError("Returning Reset did not preserve the visit baseline")
        rows.append(row)
    if [(r["axis"], r["origin"], r["channels"], r["roi"]) for r in rows] != expected:
        raise ValueError("Missing, repeated or reordered reference cases")
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--imagej-root", type=Path, required=True)
    parser.add_argument("--jdk", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    args = parser.parse_args()
    root, jdk = args.imagej_root.resolve(), args.jdk.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    classes = output / "classes"
    classes.mkdir()
    source = Path(__file__).resolve().parent
    suffix = ".exe" if (jdk / "bin/java.exe").is_file() else ""
    java, javac = (jdk / f"bin/{tool}{suffix}" for tool in ("java", "javac"))
    import os
    classpath = os.pathsep.join(map(str, (classes, root / "classes")))
    commands = [
        [str(javac), "-encoding", "UTF-8", "-cp", str(root / "classes"),
         "-d", str(classes), *(str(source / name) for name in DRIVERS)],
        [str(java), "-Djava.awt.headless=false", "-cp", classpath,
         "ij.plugin.frame.RgbContrastNavigationReference"],
    ]
    for name, command in zip(("compile", "reference"), commands):
        result = subprocess.run(command, capture_output=True, timeout=180)
        (output / f"{name}.stdout").write_bytes(result.stdout)
        (output / f"{name}.stderr").write_bytes(result.stderr)
        (output / f"{name}.command.json").write_text(json.dumps(command, indent=2) + "\n", encoding="utf-8")
        result.check_returncode()
    rows = extract((output / "reference.stdout").read_text(encoding="utf-8"))
    pins = {}
    for name in DRIVERS:
        pins["drivers/" + name] = digest(source / name)
    for name in UPSTREAM:
        pins["sources/" + name + ".java"] = digest(root / "sources" / (name + ".java"))
        pins["classes/" + name + ".class"] = digest(root / "classes" / (name + ".class"))
    pins["jdk/java"] = digest(java)
    pins["jdk/javac"] = digest(javac)
    fixture = {
        "imagej_version": "1.54r",
        "driver": "Actual ImagePlus.setPosition and ContrastAdjuster setup/mapping/masking/reset; widget presentation bypassed; no AWT event delivery",
        "shape_tczyxs": [2, 2, 2, 8, 256, 3],
        "source_formula": ["(x+11*c+23*z+41*t)&255", "(73*x+19*y+17*c+31*z+47*t)&255", "(255-x+53*c+59*z+61*t)&255"],
        "axis_order": ["t", "c", "z"],
        "selection_order": ["none", "rectangle", "irregular_mask", "empty_mask", "line", "points", "polyline", "angle", "oval"],
        "ranges": {"first": [10.9, 40.1], "changed_selection": [20.25, 200.75], "other_plane": [-30.25, 400.5], "return": [-10.2, 160.9]},
        "reference_stdout_sha256": digest(output / "reference.stdout"),
        "pins": pins,
        "cases": rows,
    }
    args.fixture.write_text(json.dumps(fixture, indent=2) + "\n", encoding="utf-8")
    print(f"Captured {len(rows)} actual ImageJ navigation/selection/reset cases")


if __name__ == "__main__":
    main()
