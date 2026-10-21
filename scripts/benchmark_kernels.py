"""Compare native processing on deterministic fixtures without an active job."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import sys
import time

import imagejxx as ij
import imagejxx._native as native
import numpy as np


def array_hash(array):
    return hashlib.sha256(np.ascontiguousarray(array).tobytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--repeats", type=int, default=40)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--build-label", default="unspecified")
    parser.add_argument("--entrypoint", choices=("raw", "prepare"), default="raw")
    parser.add_argument("--suite", choices=("filters", "processing", "segmentation"), default="filters")
    args = parser.parse_args()
    if args.repeats < 10 or args.warmup < 1:
        parser.error("Use at least ten timed repeats and one warmup")
    reference = None
    if args.compare:
        reference = json.loads(args.compare.read_text(encoding="utf-8"))
        if reference.get("schema_version") != 1:
            parser.error("Unsupported comparison schema")
    result = {
        "schema_version": 1,
        "scope": "Raw execute_operation or headless preparation/preview export; no active job or GUI.",
        "entrypoint": args.entrypoint,
        "suite": args.suite,
        "platform": sys.platform,
        "architecture": platform.machine(),
        "processor": os.environ.get("PROCESSOR_IDENTIFIER", platform.machine()),
        "logical_cpus": os.cpu_count(),
        "python": sys.version,
        "numpy": np.__version__,
        "imagejxx": ij.__version__,
        "native_sha256": hashlib.sha256(Path(native.__file__).read_bytes()).hexdigest(),
        "build_label": args.build_label,
        "repeats": args.repeats,
        "warmup": args.warmup,
        "cases": {},
    }
    if reference:
        if reference.get("entrypoint", "raw") != args.entrypoint:
            parser.error("Comparison requires matching entrypoints")
        if reference.get("suite", "filters") != args.suite:
            parser.error("Comparison requires matching suites")
        for key in ("platform", "architecture", "processor", "python", "numpy", "repeats", "warmup"):
            if reference[key] != result[key]:
                parser.error(f"Comparison requires matching {key}")
    operations = {
        "gaussian_blur": {"sigma": 1.5},
        "median_filter": {"radius": 2},
        "convolve": {"kernel": np.full((7, 7), 1 / 49).tolist()},
    }
    gray_only = {"threshold", "auto_mask", "local_threshold", "clahe"}
    if args.suite == "processing":
        operations.update({
            "add": {"value": 3},
            "adjust_brightness_contrast": {"contrast": 1.1, "brightness": 3},
            "normalize": {"lower": 0, "upper": 255, "saturation": 3},
            "find_edges": {},
            "variance_filter": {"radius": 2},
            "rolling_ball_background": {"radius": 3},
            "threshold": {"lower": 100, "upper": 30000},
            "auto_mask": {"method": "otsu"},
            "local_threshold": {"method": "median", "radius": 2},
            "clahe": {"tile_size": 32, "clip_limit": 2},
        })
    if args.suite == "segmentation":
        operations = {
            "label": {"connectivity": 4},
            "fill_holes": {"connectivity": 8},
            "skeletonize": {},
            "binary_outline": {},
            "remove_small_objects": {"min_size": 12, "connectivity": 4},
            "clear_border": {"connectivity": 8},
            "distance_transform": {"spacing": [0.75, 1.5]},
            "local_maxima": {"radius": 2},
            "binary_watershed": {"maxima_radius": 1, "connectivity": 4},
        }
    rng = np.random.default_rng(20261010)
    fixtures = {
        "u16-128": rng.integers(0, 65536, size=(128, 128), dtype=np.uint16),
        "u16-256": rng.integers(0, 65536, size=(256, 256), dtype=np.uint16),
        "f32-rgb-128": rng.random((128, 128, 3), dtype=np.float32),
    }
    if args.suite == "segmentation":
        gray = rng.random((128, 128), dtype=np.float32)
        # Distance transforms need actual background for finite outputs.
        gray[gray < 0.2] = 0
        fixtures = {
            "u8-mask-128": (rng.random((128, 128)) > 0.45).astype(np.uint8) * 255,
            "u8-mask-256": (rng.random((256, 256)) > 0.35).astype(np.uint8) * 255,
            "f32-gray-128": gray,
        }
    for fixture_name, pixels in fixtures.items():
        input_hash = array_hash(pixels)
        app = ij.Application()
        app.adopt(ij.Document(ij.Dataset(pixels, axes="YX" if pixels.ndim == 2 else "YXS")))
        target = app.resolve_target()
        for operation, parameters in operations.items():
            if pixels.ndim != 2 and operation in gray_only:
                continue
            case_name = f"{fixture_name}/{operation}"
            case = {
                "shape": list(pixels.shape),
                "dtype": str(pixels.dtype),
                "input_sha256": input_hash,
                "operation": operation,
                "parameters": parameters,
            }
            before = reference["cases"].get(case_name) if reference else None
            if reference and before is None:
                parser.error(f"Missing comparison case {case_name}")
            if before:
                for key, value in case.items():
                    if value != before[key]:
                        parser.error(f"{case_name}: comparison requires matching {key}")
            def evaluate():
                if args.entrypoint == "prepare":
                    return app.prepare(operation, target, parameters).preview.dataset.plane()
                return ij.execute_operation(operation, pixels, parameters)

            for _ in range(args.warmup):
                output = evaluate()
            elapsed = []
            for _ in range(args.repeats):
                start = time.perf_counter_ns()
                output = evaluate()
                elapsed.append((time.perf_counter_ns() - start) / 1_000_000)
            case.update(
                median_ms=statistics.median(elapsed),
                p95_ms=float(np.percentile(elapsed, 95)),
                samples_ms=elapsed,
                output_dtype=str(output.dtype),
                output_sha256=array_hash(output),
            )
            if array_hash(pixels) != input_hash:
                raise RuntimeError(f"{case_name}: input changed")
            if before:
                if (case["output_dtype"], case["output_sha256"]) != (
                    before["output_dtype"], before["output_sha256"]
                ):
                    raise RuntimeError(f"{case_name}: numerical output changed")
                case["median_ratio"] = case["median_ms"] / before["median_ms"]
                case["p95_ratio"] = case["p95_ms"] / before["p95_ms"]
            result["cases"][case_name] = case
            print(f"{case_name}: {case['median_ms']:.3f} ms median", flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
