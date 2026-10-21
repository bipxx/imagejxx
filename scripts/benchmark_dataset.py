"""Measure native axis-plane extraction on a declared asymmetric NumPy fixture."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform
import time

import imagejxx as ij
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=100)
    args = parser.parse_args()
    if args.repeats < 10:
        parser.error("Use at least ten repeats")
    shape = (64, 128, 192)
    data = np.arange(np.prod(shape), dtype=np.uint16).reshape(shape)
    start = time.perf_counter()
    dataset = ij.Dataset(data, axes="ZYX")
    result = {
        "schema_version": 1,
        "platform": platform.platform(),
        "processor": platform.processor(),
        "python": platform.python_version(),
        "imagejxx": ij.__version__,
        "fixture": {"axes": "ZYX", "shape": shape, "dtype": "uint16"},
        "repeats": args.repeats,
        "construction_ms": (time.perf_counter() - start) * 1000,
        "scope": "Native plane extraction plus detached NumPy output; not GUI input latency or upstream parity.",
        "planes": {},
    }
    for view, coordinate in [("xy", 32), ("xz", 64), ("yz", 96)]:
        dataset.plane(view, coordinate)
        elapsed = []
        for _ in range(args.repeats):
            start = time.perf_counter()
            dataset.plane(view, coordinate)
            elapsed.append((time.perf_counter() - start) * 1000)
        result["planes"][view] = {
            "median_ms": float(np.median(elapsed)),
            "p95_ms": float(np.percentile(elapsed, 95)),
            "maximum_ms": float(max(elapsed)),
        }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result["planes"], indent=2))


if __name__ == "__main__":
    main()
