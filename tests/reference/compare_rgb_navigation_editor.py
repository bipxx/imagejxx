"""Compare six real native editor round trips with the ImageJ reference.

Run with an installed imagejxx Python UI and NumPy. This is an explicit gap probe,
outside the passing regression suite: exit 1 means a numerical mismatch. It covers
all three navigation axes in both directions, All components and none -> rectangle
selection only. It does not claim coverage of all 378 upstream reference cases.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)

    import numpy as np
    import imagejxx as ij
    from imagejxx import _native, _desktop
    from imagejxx.ui.app import Editor

    fixture = json.loads(args.fixture.read_bytes())
    results = []
    for row in fixture["cases"]:
        if row["roi"] != 0 or row["channels"] != 7:
            continue
        t, c, z, y, x = np.indices((2, 2, 2, 8, 256))
        pixels = np.stack(((x+11*c+23*z+41*t)&255,
                           (73*x+19*y+17*c+31*z+47*t)&255,
                           (255-x+53*c+59*z+61*t)&255), axis=-1).astype(np.uint8)
        owner = ij.Document(ij.Dataset(pixels, axes="TCZYXS"))
        a = [row["origin"]]*3
        b = a.copy()
        b[row["axis"]] = 1-row["origin"]

        def position(p):
            view = owner.snapshot().display
            view.t, view.c = p[:2]
            view.crosshair = [0, 0, p[2]]
            owner.set_display(view)

        position(a)
        editor = Editor(preferences=output / f"prefs-{row['axis']}-{row['origin']}.json")
        editor._editor.add_virtual_document(owner)

        def current_hash():
            editor.action("show_channel_display")
            editor.action("wait_jobs")
            report = editor.render_headless(frames=2)
            values = report["ordinary_pixels"]["readback"]["source"]["pixels"]
            return hashlib.sha256(np.array(values, np.uint8).tobytes()).hexdigest()

        def mapping(lo, hi):
            editor.action("contrast_range", minimum=lo, maximum=hi)
            editor.action("wait_jobs")

        try:
            editor.action("show_contrast")
            editor.action("contrast_rgb_stack", enabled=True)
            mapping(10.9, 40.1)
            observations = {"first_map_a": current_hash()}
            owner.select(ij.Selection("rectangle", [(17, 0), (236, 8)]))
            editor.action("wait_jobs")
            mapping(20.25, 200.75)
            observations["changed_selection_map_a"] = current_hash()
            editor.action("contrast_reset")
            editor.action("wait_jobs")
            observations["selection_reset_a"] = current_hash()
            mapping(20.25, 200.75)
            position(b)
            editor.action("wait_jobs")
            observations["first_visit_b"] = current_hash()
            mapping(-30.25, 400.5)
            observations["mapped_b"] = current_hash()
            position(a)
            editor.action("wait_jobs")
            editor.action("contrast_reset")
            editor.action("wait_jobs")
            observations["return_reset_a"] = current_hash()
            mapping(-10.2, 160.9)
            observations["return_remap_a"] = current_hash()
            expected = dict(row)
            expected["selection_reset_a_sha256"] = row["original_a_sha256"]
            expected["first_visit_b_sha256"] = row["original_b_sha256"]
            checks = {key: {"actual": value, "expected": expected[key+"_sha256"],
                            "matches": value == expected[key+"_sha256"]}
                      for key, value in observations.items()}
            checks["source_pixels_unchanged"] = {"matches": np.array_equal(
                owner.snapshot().dataset.numpy("TCZYXS"), pixels)}
            results.append({key: row[key] for key in ("axis", "origin", "channels", "roi")} | {"checks": checks})
        finally:
            editor.action("close_contrast")

    if len(results) != 6:
        raise ValueError("Reference is missing one of the six navigation cases")
    matches = sum(check["matches"] for row in results for check in row["checks"].values())
    total = sum(len(row["checks"]) for row in results)
    report = {
        "state": "matched" if matches == total else "mismatched",
        "python": sys.executable,
        "fixture_sha256": hashlib.sha256(args.fixture.read_bytes()).hexdigest(),
        "products": {str(Path(m.__file__).resolve()): hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest()
                     for m in (ij, _native, _desktop)},
        "matches": matches, "checks": total, "cases": results,
    }
    (output / "report.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(f"Native RGB navigation: {matches}/{total} checks match in six cases")
    return 0 if matches == total else 1


if __name__ == "__main__":
    raise SystemExit(main())
