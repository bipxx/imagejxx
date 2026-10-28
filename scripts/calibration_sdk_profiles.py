"""Write a fresh text carrier for the installed-only C++ SDK profile consumer.

The references are immutable pinned observations, not native-generated oracles.
This command creates no compiler cache, SDK or environment.
"""
from pathlib import Path
import argparse
import gzip
import hashlib
import json
import math

ROOT = Path(__file__).resolve().parents[1]
REFERENCES = (
    ("G034-calibration-table-plots-reference001.jsonl.gz", 128,
     "7629ecacdda4bd827e3144d179c755a5b5bdc79b60cb37da94803972abcce3c7"),
    ("G034-intensity-fit-plots-reference001.jsonl.gz", 43,
     "b515b0f371e7b0bd32eced75f42c1cc6039d759537f1b368364bb9bba230a071"),
)


def write_profiles(destination):
    cases = []
    for name, count, expected in REFERENCES:
        raw = (ROOT / "docs/parity/reviews" / name).read_bytes()
        if hashlib.sha256(raw).hexdigest() != expected:
            raise ValueError(f"Reference fingerprint changed: {name}")
        rows = [json.loads(line) for line in gzip.decompress(raw).splitlines()]
        if len(rows) != count:
            raise ValueError(f"Unexpected reference count: {name}")
        cases.append(rows)
    lines = ["imagejxx-sdk-profiles-v1 128 43"]

    def numbers(values):
        if not all(isinstance(v, (int, float)) and math.isfinite(v) for v in values):
            raise ValueError("Expected finite reference numbers")
        lines.append(" ".join(format(v, ".17g") for v in values))

    for case in cases[0]:
        coefficients, pattern = case["coefficients"], case["pattern_words"]
        lines.append(f'{case["kind"]} {case["function"]} {case["bit_depth"]} '
                     f'{int(case["zero_clip"])} {int(case["inverted_lut"])} '
                     f'{len(coefficients)} {len(pattern)}')
        numbers(coefficients)
        numbers(pattern)
        if len(case["curve_x"]) != 256 or len(case["curve_words"]) != 256:
            raise ValueError("Unexpected table sampling count")
        numbers(case["curve_x"])
        numbers(case["curve_words"])
    for case in cases[1]:
        n, count = len(case["measured"]), len(case["curve_x"])
        if len(case["known"]) != n or len(case["curve_y"]) != count:
            raise ValueError("Unpaired fit plot reference")
        lines.append(f'{case["function"]} {case["points"]} '
                     f'{len(case["coefficients"])} {n} {count}')
        for key in ("coefficients", "measured", "known", "curve_x"):
            numbers(case[key])
        numbers([int(v is not None) for v in case["curve_y"]])
        numbers([v if v is not None else 0 for v in case["curve_y"]])
        numbers(case["limits"])
    raw = ("\n".join(lines) + "\n").encode("ascii")
    with Path(destination).open("xb") as stream:
        stream.write(raw)
    return {"table_profiles": 128, "fit_profiles": 43,
            "fit_dtype_cells": 86, "bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest()}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    print(json.dumps(write_profiles(parser.parse_args().destination)))
