"""Add partial parity schemas and seed only accepted binary/conversion evidence."""

from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location(
    "imagejxx_migration_schema", Path(__file__).with_name("parity_schema.py")
)
schema = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(schema)

BINARY_RUN = "m2-m6-binary-options-windows-2026-10-10"
CONVERSION_RUN = "m2-m6-conversion-options-windows-2026-10-10"
BINARY_CANDIDATES = {
    "erode": "R-1c64e233b004251790a0b858",
    "dilate": "R-ed9540786eff919bacb61f49",
    "open": "R-d10fef835cb9c70a2a444355",
    "close": "R-447b8b162d4b1747796e14b9",
}
BINARY_OPTIONS = "R-3f4eb5022147bc62f6e2e994"
CONVERSION_OPTIONS = "R-78a324f26fb627a46fd5f06d"


def read_documents(directory):
    documents = {}
    for name in (*schema.REFERENCE_FILES, *schema.COLLECTIONS):
        target = directory / name
        if target.is_file():
            documents[name] = json.loads(target.read_text(encoding="utf-8"))
    schema.validate_documents(documents)
    return documents


def sha_file(root, path):
    return hashlib.sha256((root / path).read_bytes()).hexdigest()


def control(name, kind, default, enabled="always", units="", **limits):
    return {
        "name": name,
        "type": kind,
        "default": default,
        "enabled_when": enabled,
        "units": units,
        **limits,
    }


def accepted_corpus(root, documents, inventory, name, run_id):
    """Reject stale fixtures/pins or invented accepted runs before migration."""
    runs = [r for r in inventory["verification_runs"] if r["id"] == run_id]
    if len(runs) != 1:
        raise ValueError(f"Missing unique accepted run: {run_id}")
    run = runs[0]
    upstream = run["upstream_reference"]
    fixture_path = f"tests/fixtures/{name}-imagej154r.json"
    fixture = json.loads((root / fixture_path).read_text(encoding="utf-8"))
    refs = documents["references.json"]
    count = len(fixture["cases"])
    if (
        upstream["fixture"] != fixture_path
        or upstream["fixture_sha256"] != sha_file(root, fixture_path)
        or upstream["matched_cases"] != count
        or upstream["mismatches"] != 0
        or upstream["reference_errors"] != 0
        or upstream["commit"] != refs["imagej"]["commit"]
        or fixture["imagej_commit"] != refs["imagej"]["commit"]
        or fixture["source_archive_sha256"] != refs["imagej"]["sha256"]
        or fixture["fiji_runtime_archive_sha256"] != refs["fiji"]["sha256"]
        or fixture["scope"] != upstream["scope"]
    ):
        raise ValueError(f"Accepted {name} fixture identity/count/scope does not match")
    for suite in ("native", "python"):
        if run[suite]["failed"] != 0 or run[suite]["passed"] <= 0:
            raise ValueError(
                f"Accepted {name} run has failed or empty {suite} evidence"
            )
    if name == "binary":
        members = [
            {
                "path": fixture["source_member"],
                "sha256": fixture["source_member_sha256"],
            }
        ]
    else:
        prefix = f"ImageJ-{fixture['imagej_commit']}/"
        members = [
            {"path": prefix + path, "sha256": digest}
            for path, digest in sorted(fixture["source_members_sha256"].items())
        ]
    reference = {
        "reference_id": refs["reference_id"],
        "component_id": "imagej-core",
        "archive_sha256": refs["imagej"]["sha256"],
        "revision": refs["imagej"]["commit"],
        "scope": fixture["scope"],
        "source_members": members,
    }
    record = {
        "id": f"F.{name}-imagej154r",
        "requirement_ids": [],
        "path": fixture_path,
        "sha256": upstream["fixture_sha256"],
        "case_count": count,
        "reference": reference,
        "scope": fixture["scope"],
        "generator": {
            "path": fixture["generator"],
            "sha256": sha_file(root, fixture["generator"]),
            "driver_sha256": fixture["driver_sha256"],
        },
        "comparison": {"method": "exact", "atol": 0, "rtol": 0},
        "capture_status": "captured",
        "match_status": "matched",
        "verification_run_ids": [run_id],
        "license": {
            "decision": "pending",
            "evidence": [],
            "reason": "Owned fixture capture and numerical match do not approve upstream source, runtime, or generated reference material for incorporation.",
        },
        "remaining": [
            "Review exact fixture/generator/runtime license provenance and distribution boundaries; P01c certification remains required.",
            *run["limits"],
        ],
    }
    return run, record


def requirement(
    parent,
    command,
    parameter,
    candidate_ids,
    run,
    fixture,
    contract,
    settings_key=None,
    settings_default=None,
    kind="option",
):
    name = parameter["name"]
    identifier = f"{parent}.{command}.{name}"
    binary = "binary" in command
    header = (
        "include/imagejxx/binary.hpp" if binary else "include/imagejxx/conversion.hpp"
    )
    test = (
        "tests/python/test_binary_options.py"
        if binary
        else "tests/python/test_conversion_options.py"
    )
    cpp_command = "binary_morphology" if binary else command
    if kind == "setting":
        cpp_command = (
            "Settings::binary_options" if binary else "Settings::conversion_options"
        )
    python_command = command if kind == "option" else cpp_command.replace("::", ".")
    row = {
        "id": identifier,
        "parent_id": parent,
        "milestones": ["M2", "M6"] if parent != "G060" else ["M1", "M5", "M6"],
        "kind": kind,
        "title": f"{command}: {name}",
        "required_work": f"Preserve the implemented {name} contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.",
        "candidate_ids": candidate_ids,
        "reference": deepcopy(fixture["reference"]),
        "contract": {**deepcopy(contract), "parameters": [deepcopy(parameter)]},
        "interfaces": {
            "cpp": {
                "applicable": True,
                "entry_points": [cpp_command],
                "evidence": [header, run["native"]["report"]],
            },
            "python": {
                "applicable": True,
                "entry_points": [python_command],
                "evidence": ["src/imagejxx/__init__.py", test, run["python"]["report"]],
            },
            "imgui": {
                "applicable": True,
                "entry_points": [
                    "Shared ImGui preferences"
                    if kind == "setting"
                    else f"Operation form: {command}"
                ],
                "evidence": ["src/desktop/window.cpp", run["packaged_desktop_report"]],
            },
        },
        "settings_consumers": [],
        "fixture_ids": [fixture["id"]] if kind == "option" else [],
        "verification_run_ids": [run["id"]],
        "implementation_status": "partial",
        "verification_status": "partial",
        "commercial": {
            "decision": "pending",
            "evidence": [],
            "reason": "Owned native equivalent incorporates no upstream processing component. Accepted shipped-dependency checks do not approve the linked upstream candidates, components, resources or reference fixtures.",
        },
        "provenance": {
            "implementation": "owned_native_equivalent",
            "incorporated_components": [],
        },
        "remaining": [
            "P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.",
            "P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.",
            *run["limits"],
        ],
    }
    if not candidate_ids:
        row["candidate_link_reason"] = (
            "Direct processor/native command; static discovery has not enumerated the hard-coded ImageJ menu command. Full runtime reconciliation remains required."
        )
    if settings_key:
        row["settings_consumers"] = [
            {
                "key": settings_key,
                "consumer": "Settings option snapshot, shared operation draft, frozen workflow parameters and preference reset/import/export/reopen",
            }
        ]
    if settings_default is not None:
        row["settings_default"] = settings_default
    return row


def seed_current(root, documents):
    inventory = json.loads(
        (root / "docs/parity/inventory.json").read_text(encoding="utf-8")
    )
    binary_run, binary_fixture = accepted_corpus(
        root, documents, inventory, "binary", BINARY_RUN
    )
    conversion_run, conversion_fixture = accepted_corpus(
        root, documents, inventory, "conversion", CONVERSION_RUN
    )
    requirements = []
    binary_contract = {
        "pixel_types": ["uint8"],
        "samples": ["one sample; values exactly 0 or 255"],
        "axes": ["XY planes independently across selected C/Z/T"],
        "targets": ["active plane", "whole dataset", "C/Z/T plane range", "ROI target"],
        "roi": [
            "Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only"
        ],
        "border": [
            "Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries"
        ],
        "nonfinite": "uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation",
        "geometry": "Preserve dimensions, samples, selection geometry/style and one-step publication/undo",
    }
    binary_parameters = [
        control("iterations", "integer", 1, minimum=1, maximum=100),
        control("neighbors", "integer", 1, minimum=1, maximum=8),
        control("black_background", "boolean", True),
        control(
            "pad_edges",
            "boolean",
            False,
            "Erosion stages only; accepted and retained for dilation",
        ),
    ]
    for command, candidate in BINARY_CANDIDATES.items():
        for parameter in binary_parameters:
            requirements.append(
                requirement(
                    "G052",
                    f"binary_{command}",
                    parameter,
                    [candidate, BINARY_OPTIONS],
                    binary_run,
                    binary_fixture,
                    binary_contract,
                    settings_key="binary_" + parameter["name"],
                )
            )
    for parameter in binary_parameters:
        requirements.append(
            requirement(
                "G008",
                "binary-defaults",
                parameter,
                [BINARY_OPTIONS],
                binary_run,
                binary_fixture,
                binary_contract,
                settings_key="binary_" + parameter["name"],
                kind="setting",
            )
        )
    conversion_contract = {
        "pixel_types": [
            "uint8",
            "uint16",
            "float32",
            "float64 native extension outside reference corpus",
        ],
        "samples": [
            "grayscale",
            "uint8 RGB",
            "uint8 RGBA native extension; ignore alpha",
        ],
        "axes": ["XY planes independently across complete C/Z/T dataset"],
        "targets": [
            "complete whole dataset only for Application type conversion; direct arrays are single images"
        ],
        "roi": [
            "Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling"
        ],
        "border": ["Pointwise conversion; no spatial border sampling"],
        "nonfinite": "Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required",
        "geometry": "Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step",
    }
    conversion_parameters = [
        control(
            "dtype",
            "string",
            "uint8",
            choices=["uint8", "uint16", "float32", "float64"],
        ),
        control("scale", "boolean", True),
        control("weighted_rgb", "boolean", False),
        control("red_weight", "number", 0.299, "weighted_rgb and RGB/RGBA input"),
        control("green_weight", "number", 0.587, "weighted_rgb and RGB/RGBA input"),
        control("blue_weight", "number", 0.114, "weighted_rgb and RGB/RGBA input"),
        control(
            "range_policy", "string", "data", choices=["data", "dtype", "explicit"]
        ),
        control(
            "range_min",
            "number",
            0.0,
            "range_policy == explicit",
            units="source intensity",
        ),
        control(
            "range_max",
            "number",
            1.0,
            "range_policy == explicit",
            units="source intensity",
        ),
    ]
    for parameter in conversion_parameters:
        name = parameter["name"]
        requirements.append(
            requirement(
                "G060",
                "convert_image",
                parameter,
                [],
                conversion_run,
                conversion_fixture,
                conversion_contract,
                settings_key="conversion_" + name
                if name not in {"dtype", "range_min", "range_max"}
                else None,
                settings_default="display" if name == "range_policy" else None,
            )
        )
    inversion_contract = {
        **conversion_contract,
        "pixel_types": ["uint16"],
        "samples": ["one sample"],
        "targets": ["active plane", "whole dataset", "C/Z/T plane range", "ROI target"],
        "roi": [
            "Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only"
        ],
        "geometry": "Preserve dimensions/type/sample/selection; pointwise inversion, one-step publication/undo",
    }
    inversion_parameters = [
        control("full_range_inversions", "boolean", False),
        control(
            "unsigned16_bits",
            "integer",
            0,
            "full_range_inversions; 0 uses 16 bits",
            units="bits",
            choices=[0, 8, 10, 12, 14, 15, 16],
        ),
    ]
    for parameter in inversion_parameters:
        key = (
            "conversion_full_range_inversions"
            if parameter["name"] == "full_range_inversions"
            else "conversion_unsigned16_range"
        )
        requirements.append(
            requirement(
                "G060",
                "invert_uint16",
                parameter,
                [],
                conversion_run,
                conversion_fixture,
                inversion_contract,
                settings_key=key,
            )
        )
    conversion_settings = [
        p
        for p in conversion_parameters
        if p["name"] not in {"dtype", "range_min", "range_max"}
    ]
    conversion_settings = deepcopy(conversion_settings)
    next(p for p in conversion_settings if p["name"] == "range_policy").update(
        default="display", choices=["display", "data", "dtype"]
    )
    conversion_settings.extend(
        [
            inversion_parameters[0],
            control(
                "unsigned16_range",
                "string",
                "auto",
                choices=["auto", "8", "10", "12", "14", "15", "16"],
            ),
        ]
    )
    for parameter in conversion_settings:
        row = requirement(
            "G007",
            "conversion-defaults",
            parameter,
            [CONVERSION_OPTIONS],
            conversion_run,
            conversion_fixture,
            conversion_contract,
            settings_key="conversion_" + parameter["name"],
            kind="setting",
        )
        row["reference"]["scope"] += (
            " Preference consumption evidence is owned-native UI/API evidence; full ImageJ Options dialog equivalence remains unverified."
        )
        requirements.append(row)
    for fixture in (binary_fixture, conversion_fixture):
        fixture["requirement_ids"] = sorted(
            r["id"] for r in requirements if fixture["id"] in r["fixture_ids"]
        )
    return requirements, [binary_fixture, conversion_fixture]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    documents = read_documents(args.root / "docs/parity")
    requirements, fixtures = seed_current(args.root, documents)
    proposed = schema.migrate_documents(documents, requirements, fixtures)
    changed = schema.write_documents(args.root / "docs/parity", proposed, args.check)
    counts = schema.validate_documents(proposed)
    print(f"Partial parity structures: {counts}; changed manifests: {changed}")
    print(
        "Commercial/component/resource reviews and strict reference certification remain pending."
    )


if __name__ == "__main__":
    main()
