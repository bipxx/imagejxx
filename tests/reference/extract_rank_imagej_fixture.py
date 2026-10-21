"""Copy retained ImageJ pixels into the native finite-scalar rank corpus.

This is a byte-selection tool, not a numerical oracle: no filtering, expected
pixel calculation, Java, or imagejxx import occurs here. The complete original
16-case capture remains untouched. RGB/nonfinite/dialog/preview rows remain
listed in provenance while this core-only corpus selects finite scalar runs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[2]
RAW = "build/references/rank-filter-imagej154r-001/capture-001/reference.stdout"
RAW_SHA = "f2a3677079d355b53339b11612fc39ca809e5b070b22caa80a5d4fcc6b739df5"
PLAN = "build/references/rank-filter-imagej154r-001/capture-001/input-plan.json"
PLAN_SHA = "1f64bf082e5e8ccd819fdec87598d765acb571aaaf6fa15b42abd0e3547e4b60"
INPUTS = "build/references/rank-filter-imagej154r-001/capture-001/execution-inputs.json"
INPUTS_SHA = "5bd642c6720ff28059769757a82123c51a521576e0ec0829421c198953a750d8"
VERIFIED = "build/references/rank-filter-imagej154r-001/prepare-002/verified-inputs.json"
VERIFIED_SHA = "0219aa5790d49120916d916dc27fa2e5ac8678443b8f6074f446db4aa77944d1"
IDS = [f"K{i:02d}" for i in range(1, 17)]
COUNTS = [0, 3, 3, 3, 3, 8, 1, 1, 108, 8, 12, 4, 12, 8, 0, 0]


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode()


def pin(relative, expected):
    path = ROOT / relative
    raw = path.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != expected:
        raise ValueError(f"retained input changed: {relative}")
    return raw, {"path": relative, "bytes": len(raw), "sha256": digest}


def finite_scalar(spec):
    dtype = spec["dtype"]
    if dtype not in ("uint8", "uint16", "float32"):
        return False
    if dtype == "float32":
        for plane in spec["planes"]:
            raw = bytes.fromhex(plane["bits_hex"])
            for (bits,) in struct.iter_unpack("<I", raw):
                if bits & 0x7f800000 == 0x7f800000:
                    return False
    return True


def copy_run(case_id, observation):
    spec = observation["input"]
    fixture = spec["fixture"]
    if not finite_scalar(fixture):
        raise ValueError("nonfinite/non-scalar run cannot enter native corpus")
    before, after = observation["before"], observation["after"]
    if before["shape_tczyx"] != fixture["shape_tczyx"] or before["position_tcz"] != fixture["position_tcz"]:
        raise ValueError("observed input metadata does not match retained request")
    coordinates = lambda planes: [(p["t"], p["c"], p["z"]) for p in planes]
    if coordinates(before["planes"]) != coordinates(after["planes"]) or coordinates(before["planes"]) != coordinates(fixture["planes"]):
        raise ValueError("observed plane coordinates differ")
    planes = []
    for request, first, last in zip(fixture["planes"], before["planes"], after["planes"]):
        if request["bits_hex"] != first["bits_hex"]:
            raise ValueError("observed source bytes differ from retained request")
        planes.append({"tcz": [first["t"], first["c"], first["z"]],
                       "before_hex": first["bits_hex"], "after_hex": last["bits_hex"]})
    return {"id": case_id + "/" + spec["name"], "source_case_id": case_id,
            "source_observation_sha256": hashlib.sha256(canonical(observation)).hexdigest(),
            "dtype": fixture["dtype"], "shape_tczyx": fixture["shape_tczyx"],
            "position_tcz": fixture["position_tcz"], "roi": fixture["roi"],
            "dispatch": spec["dispatch"], "all_stack": spec["all_stack"],
            "method": spec["method"], "radius": spec["radius"], "planes": planes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--supplement", help="Retained one-case JSONL supplemental observation")
    parser.add_argument("--supplement-sha256")
    parser.add_argument("--output", default="tests/fixtures/rank_imagej_154r_001.json")
    args = parser.parse_args()
    if bool(args.supplement) != bool(args.supplement_sha256):
        parser.error("--supplement and --supplement-sha256 must be supplied together")
    raw, raw_pin = pin(RAW, RAW_SHA)
    plan_raw, plan_pin = pin(PLAN, PLAN_SHA)
    _, inputs_pin = pin(INPUTS, INPUTS_SHA)
    verified_raw, verified_pin = pin(VERIFIED, VERIFIED_SHA)
    rows = [json.loads(line) for line in raw.splitlines()]
    plan = json.loads(plan_raw)
    if [r["id"] for r in rows] != IDS or plan["ordered_case_ids"] != IDS:
        raise ValueError("original sixteen case membership/order changed")
    if [r["input_contract"] for r in rows] != plan["cases"]:
        raise ValueError("observed input contracts differ from consumed source plan")
    # Retain source row hashes even for cases outside this core API boundary.
    inventory = []
    runs = []
    for index, row in enumerate(rows):
        expected_count = COUNTS[index]
        original_runs = row["input_contract"].get("runs", [])
        if len(original_runs) != expected_count:
            raise ValueError("original 174 numerical-run membership changed")
        inventory.append({"id": row["id"], "numerical_run_count": expected_count,
                          "row_sha256": hashlib.sha256(canonical(row)).hexdigest(),
                          "native_scope": "geometry" if index == 0 else "finite_scalar" if index in (*range(1, 11), 13) else "observational_only"})
        if index in (*range(1, 11), 13):
            if [o["input"] for o in row["observations"]] != original_runs:
                raise ValueError("observed numerical requests differ from original plan")
            runs.extend(copy_run(row["id"], o) for o in row["observations"])
    if len(runs) != 158:
        raise ValueError("finite scalar native selection must contain exactly 158 original runs")
    kernels = []
    for observation in rows[0]["observations"]:
        wire = observation["line_radii"]
        kernels.append({"id": "K01/" + str(observation["radius"]), "radius": observation["radius"],
                        "half_widths": wire[1:-2:2], "point_count": observation["point_count"],
                        "offsets_xy": observation["offsets_xy"]})
    supplement_pin = None
    if args.supplement:
        supplement_raw, supplement_pin = pin(args.supplement, args.supplement_sha256)
        supplemental_rows = [json.loads(line) for line in supplement_raw.splitlines()]
        if len(supplemental_rows) != 1:
            raise ValueError("exactly one supplemental source case required")
        supplemental = supplemental_rows[0]
        if supplemental["contract"] != "g042.rank-filter-signed-zero-reference.001" or supplemental["id"] != "SZ01":
            raise ValueError("unexpected supplemental source identity")
        observations = supplemental["observations"]
        if [o["input"] for o in observations] != supplemental["inputs"]:
            raise ValueError("supplement observed requests differ from consumed inputs")
        if len(observations) != 3 or [o["input"]["method"] for o in observations] != ["minimum", "maximum", "median"]:
            raise ValueError("supplement requires exact three finite signed-zero extrema/median runs")
        for observation in observations:
            if observation["input"]["radius"] != 2 or observation["input"]["fixture"]["shape_tczyx"] != [1, 1, 1, 7, 9]:
                raise ValueError("supplement radius or shape changed")
            runs.append(copy_run(supplemental["id"], observation))
    verified = json.loads(verified_raw)
    result = {"schema_version": 1, "kind": "retained_imagej_native_rank_fixture",
              "imagej_version": "1.54r", "imagej_commit": verified["reference"]["commit"],
              "reference": verified["reference"],
              "provenance": {"selection": "literal observed bytes only; no numerical regeneration",
                             "raw": raw_pin, "input_plan": plan_pin, "execution_inputs": inputs_pin,
                             "verified_inputs": verified_pin, "supplement": supplement_pin,
                             "original_case_ids": IDS, "original_numerical_run_count": 174,
                             "original_case_inventory": inventory,
                             "observational_only": {"K12": "RGB outside finite scalar S1", "K13": "nonfinite outside finite scalar S1",
                                                    "K15": "dialog lifecycle outside numerical API", "K16": "preview lifecycle outside numerical API"},
                             "source_members": verified["known_source_members"],
                             "rank_source_members": verified["rank_source_members"]},
              "scope": "Core numerical/rectangle API only; no stack dispatch, mask restoration, editor, commercial, or platform acceptance",
              "kernel_count": len(kernels), "original_selected_run_count": 158,
              "supplement_run_count": 3 if supplement_pin else 0, "run_count": len(runs),
              "kernels": kernels, "runs": runs}
    output = ROOT / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=True, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({"output": args.output, "bytes": output.stat().st_size,
                      "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
                      "kernel_count": len(kernels), "original_selected_run_count": 158,
                      "supplement_run_count": result["supplement_run_count"], "run_count": len(runs)}, sort_keys=True))


if __name__ == "__main__":
    main()
