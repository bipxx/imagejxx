"""Generate project-owned XYZ navigation expectations without ImageJ execution."""
from __future__ import annotations

import argparse
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import struct


def pixels(coordinates):
    return b"".join(struct.pack("<f", 10000 * z + 100 * y + x)
                    for x, y, z in coordinates)


def expected(xyz):
    x, y, z = xyz
    planes = {
        "XY": ((u, v, z) for v in range(17) for u in range(32)),
        "XZ": ((u, y, v) for v in range(9) for u in range(32)),
        "YZ": ((x, u, v) for v in range(9) for u in range(17)),
    }
    return {
        "crosshair_xyz": list(xyz),
        "landmark_value": 10000 * z + 100 * y + x,
        "world_xyz": [float(Decimal(10) - Decimal("1.2") * y),
                      float(Decimal(-5) + Decimal("0.4") * x),
                      float(Decimal(2) + Decimal(3) * z)],
        "plane_sha256": {name: hashlib.sha256(pixels(coords)).hexdigest()
                         for name, coords in planes.items()},
    }


def generate():
    initial = (7, 5, 3)
    cases = [{"id": "initial", "input": {"crosshair_xyz": list(initial)},
              "expected": expected(initial)}]
    limits = (31, 16, 8)
    for plane, axis in (("XY", 2), ("XZ", 1), ("YZ", 0)):
        for key, delta in (("Up", 1), ("PageUp", 1), ("Down", -1), ("PageDown", -1)):
            xyz = list(initial)
            xyz[axis] += delta
            cases.append({"id": f"plain-{plane}-{key}",
                          "input": {"crosshair_xyz": list(initial), "plane": plane,
                                    "key": key, "modifiers": []}, "expected": expected(xyz)})
        for key, shifted_axis, delta in (("Left", 0, -1), ("Right", 0, 1),
                                         ("Up", 1, -1), ("Down", 1, 1)):
            xyz = list(initial)
            xyz[shifted_axis] += delta
            cases.append({"id": f"shift-{plane}-{key}",
                          "input": {"crosshair_xyz": list(initial), "plane": plane,
                                    "key": key, "modifiers": ["Shift"]}, "expected": expected(xyz)})
        for coordinate, key in ((0, "Down"), (limits[axis], "Up")):
            xyz = list(initial)
            xyz[axis] = coordinate
            cases.append({"id": f"bound-plain-{plane}-{key}",
                          "input": {"crosshair_xyz": xyz, "plane": plane,
                                    "key": key, "modifiers": []}, "expected": expected(xyz)})
        for key, shifted_axis, coordinate in (("Left", 0, 0), ("Right", 0, 31),
                                              ("Up", 1, 0), ("Down", 1, 16)):
            xyz = list(initial)
            xyz[shifted_axis] = coordinate
            cases.append({"id": f"bound-shift-{plane}-{key}",
                          "input": {"crosshair_xyz": xyz, "plane": plane,
                                    "key": key, "modifiers": ["Shift"]}, "expected": expected(xyz)})
    return {
        "schema_version": 1, "format": "owned_cases_v1", "origin": "owned_deterministic",
        "generator": "scripts/generate_xyz_fixture.py", "reference_execution": False,
        "shape_zyx": [9, 17, 32], "dtype": "float32", "encoding": "Float32LE-ZYX-no-header",
        "formula": "10000*z+100*y+x", "spacing_xyz": [0.4, 1.2, 3.0],
        "origin_xyz": [10, -5, 2], "direction": [[0, -1, 0], [1, 0, 0], [0, 0, 1]], "unit": "um",
        "source_sha256": hashlib.sha256(pixels((x, y, z) for z in range(9)
                                              for y in range(17) for x in range(32))).hexdigest(),
        "tolerances": {"pixels": {"method": "exact", "atol": 0, "rtol": 0},
                       "indices": {"method": "exact", "atol": 0, "rtol": 0},
                       "world_xyz": {"method": "float_tolerance", "atol": 1e-12, "rtol": 0}},
        "cases": cases,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(generate(), indent=2, allow_nan=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
