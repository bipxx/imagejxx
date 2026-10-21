"""Artificial parser/omission guards only; no capture, product or real proof run."""
from copy import deepcopy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct

import pytest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("slicer_fixture_guard_tests", ROOT / "scripts/parity_fixtures.py")
fixtures = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fixtures)

PROFILES = ("down_noninteger", "up_noninteger", "equal_dimension_crop", "integer_quad_ties", "identity_no_resize")
IDS = tuple(f"{kind}.{plane}.{profile}.reverse{flags // 4}.flip{flags // 2 % 2}.transpose{flags % 2}"
    for profile in PROFILES for kind in ("u8", "u16", "f32") for plane in ("xz", "yz") for flags in range(8))
SELECTORS = ["/output/shape_tczyxs", "/output/encoding", "/output/byte_count", "/output/typed_sha256",
             "/output/scalar_spacing_float64_hex/0", "/output/scalar_spacing_float64_hex/1", "/output/scalar_spacing_float64_hex/2"]


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode()).hexdigest()


def artificial_case(case_id):
    """Input annex and invented small output schema, never an upstream golden."""
    source = fixtures._slicer_input(case_id, "artificial input")
    coordinates = [1, 3] if source["plane"] == "xz" else [1, 3, 5, 7]
    if source["reverse_order"]:
        coordinates.reverse()
    width = {"u8": 1, "u16": 2, "f32": 4}[source["type"]]
    return {"case_id": case_id, "input": source, "source_typed_sha256": "1" * 64,
        "output_shape_tczyxs": [1, 1, len(coordinates), 2, 2, 1],
        "output_encoding": {"u8": "UInt8", "u16": "UInt16LE", "f32": "Float32LE"}[source["type"]],
        "output_byte_count": len(coordinates) * 4 * width, "output_typed_sha256": "2" * 64,
        "scalar_spacing": [1.0, 2.0, 3.0], "scalar_spacing_float64_hex": ["3ff0000000000000", "4000000000000000", "4008000000000000"],
        "selected_source_coordinates": coordinates,
        "resize_branch": {"identity_no_resize": "no_resize", "equal_dimension_crop": "equal_dimension_crop"}.get(source["profile"], "bilinear_resize")}


def artificial_corpus():
    return {"schema_version": 1, "case_count": 240, "case_ids": list(IDS), "cases": [artificial_case(case_id) for case_id in IDS]}


def artificial_snapshot(kind="u8", payload=None):
    sample = {"u8": b"\x01", "u16": b"\x01\x00", "f32": b"\x00\x00\x80\x3f"}[kind]
    raw = sample * 8 if payload is None else payload
    calibration = {"spacing": [1.0, 2.0, 3.0], "spacing_float64_raw_hex": ["3ff0000000000000", "4000000000000000", "4008000000000000"],
        "origin_pixels": [10.0, -5.0, 2.0], "origin_float64_raw_hex": ["4024000000000000", "c014000000000000", "4000000000000000"],
        "unit": "um", "x_unit": "um", "y_unit": "um", "z_unit": "um", "time_unit": "s",
        "frame_interval": .75, "frame_interval_raw_hex": "3fe8000000000000", "fps": 0.0,
        "loop": False, "info": "artificial only", "invert_y": False, "value_unit": "Gray Value",
        "density_function": 20, "density_coefficients": None, "zero_clip": False, "signed_16bit": False,
        "coordinate_functions_at_zero_xyz": [-10.0, 10.0, -6.0], "coordinate_y_at_zero_with_height": 10.0}
    return {"width": 2, "height": 2, "stack_size": 2, "channels": 1, "slices": 2, "frames": 1,
        "is_hyperstack": False, "imagej_type": {"u8": 0, "u16": 1, "f32": 2}[kind], "bit_depth": {"u8": 8, "u16": 16, "f32": 32}[kind],
        "current_slice_one_based": 1, "title": "Artificial", "encoding": {"u8": "UInt8", "u16": "UInt16LE", "f32": "Float32LE"}[kind],
        "raw_axes": "ZYX", "raw_hex": raw.hex(), "storage_raw_hex": raw.hex(), "raw_byte_count": len(raw),
        "scalar_calibration": calibration, "roi": None, "processor_states": [], "slice_labels": [None, None]}


def contract_graph():
    corpus = artificial_corpus()
    projections = fixtures.project_cases("imagej_slicer_bilinear_v1", corpus)
    child = {"reference": {"scope": "artificial adapter guard"}, "contract": {}, "settings_consumers": [],
        "interfaces": {name: {"applicable": True, "entry_points": ["artificial"]} for name in ("cpp", "python", "imgui")}}
    requirements = {"G029.synthetic-slicer-guard": child}
    fixture = {"id": "F.synthetic-slicer", "path": "artificial.json", "sha256": "3" * 64, "case_count": 240,
        "reference": {"scope": "artificial adapter guard"}, "generator": {"path": "synthetic/helper.py", "sha256": "4" * 64},
        "scope": "artificial only", "comparison": {"method": "exact", "atol": 0, "rtol": 0},
        "requirement_ids": list(requirements)}
    rows = [{"case_id": IDS[index], "locator": index, "input_sha256": fingerprint(row["input"]),
             "expected_sha256": fingerprint(row["expected"]), "required_output_paths": list(SELECTORS)} for index, row in enumerate(projections)]
    contract = {"id": "FC.synthetic-slicer", "fixture_id": fixture["id"], "format": "imagej_slicer_bilinear_v1",
        "fixture_snapshot": fixtures.fixture_snapshot(fixture, requirements), "case_set": rows,
        "requirement_scopes": [{"requirement_id": next(iter(requirements)), "snapshot": fixtures.execution.frozen_snapshot(child),
            "exclusions": ["Artificial: no numerical/upstream/world acceptance."],
            "required": [{"case_id": case_id, "path": path} for case_id in IDS for path in SELECTORS]}],
        "comparison": {**fixture["comparison"], "numeric_paths": [], "units": "typed/scalar bits", "rationale": "artificial guard", "nonfinite": "reject"}}
    contract["sha256"] = fingerprint(contract)
    return fixture, requirements, projections, contract


def validate_contract(fixture, requirements, projections, contract):
    result = {"contracts_validated": 0, "required_case_assertions": 0, "missing_contracts": [], "unsupported_methods": []}
    fixtures._validate_contracts({"fixture_contracts": [contract]}, {fixture["id"]: fixture}, requirements,
        {fixture["id"]: projections}, result)
    return result


def test_new_format_preserves_legacy_owned_projection_and_known_formats():
    assert {"imagej_axis_plane_v1", "imagej_axis_range_v1", "imagej_axis_range_orientation_v1", "owned_cases_v1"} <= fixtures.FORMATS
    owned = {"schema_version": 1, "cases": [{"input": {"shape": [1, 2]}, "expected": {"pixels": [4, 5]}}]}
    assert fixtures.project_cases("owned_cases_v1", owned) == [{"locator": 0, "input": {"shape": [1, 2]}, "expected": {"pixels": [4, 5]}}]


def test_exact240_projection_and1680_selector_contract():
    assert len(IDS) == len(set(IDS)) == 240 and list(fixtures.SLICER_OUTPUT_PATHS) == SELECTORS
    fixture, requirements, projections, contract = contract_graph()
    assert len(projections) == 240
    assert validate_contract(fixture, requirements, projections, contract)["required_case_assertions"] == 1680
    assert set(projections[0]["expected"]["output"]) == {"shape_tczyxs", "encoding", "byte_count", "typed_sha256", "scalar_spacing_float64_hex"}


@pytest.mark.parametrize("mutation", ("missing", "duplicate", "reordered"))
def test_partial_duplicate_or_reordered_cases_refuse(mutation):
    corpus = artificial_corpus()
    if mutation == "missing":
        corpus["cases"].pop()
    elif mutation == "duplicate":
        corpus["cases"][1] = deepcopy(corpus["cases"][0])
    else:
        corpus["cases"][0], corpus["cases"][1] = corpus["cases"][1], corpus["cases"][0]
    with pytest.raises(ValueError):
        fixtures.project_cases("imagej_slicer_bilinear_v1", corpus)


@pytest.mark.parametrize("mutation", ("selector", "world_selector", "scalar_omission", "float_tolerance", "case_order"))
def test_contract_cannot_reduce_denominator_or_broaden_world_claim(mutation):
    fixture, requirements, projections, contract = contract_graph()
    if mutation in ("selector", "scalar_omission"):
        contract["case_set"][0]["required_output_paths"].pop()
    elif mutation == "world_selector":
        contract["case_set"][0]["required_output_paths"].append("/output/world")
    elif mutation == "float_tolerance":
        fixture["comparison"]["method"] = contract["comparison"]["method"] = "float_tolerance"
        contract["fixture_snapshot"] = fixtures.fixture_snapshot(fixture, requirements)
    else:
        contract["case_set"][0]["case_id"] = IDS[1]
    contract["sha256"] = fingerprint({key: value for key, value in contract.items() if key != "sha256"})
    with pytest.raises(ValueError):
        validate_contract(fixture, requirements, projections, contract)


@pytest.mark.parametrize("mutation", ("source_input", "bool_shape", "bit_omission", "hash_only", "nonfinite_scalar", "singleton"))
def test_portable_projection_refuses_weakened_schema(mutation):
    corpus = artificial_corpus()
    row = corpus["cases"][0]
    if mutation == "source_input": row["input"]["use_bicubic_argument"] = True
    elif mutation == "bool_shape": row["output_shape_tczyxs"][3] = True
    elif mutation == "bit_omission": row["scalar_spacing_float64_hex"].pop()
    elif mutation == "hash_only": del row["scalar_spacing"]
    elif mutation == "nonfinite_scalar": row["scalar_spacing"][0] = math.inf
    else: row["output_shape_tczyxs"][3] = 1
    with pytest.raises(ValueError): fixtures.project_cases("imagej_slicer_bilinear_v1", corpus)


@pytest.mark.parametrize("kind", ("u8", "u16", "f32"))
def test_snapshot_digest_comes_from_full_typed_bytes(kind):
    image = artificial_snapshot(kind)
    projected = fixtures._slicer_snapshot(image, kind, "artificial")
    assert projected["typed_sha256"] == hashlib.sha256(bytes.fromhex(image["raw_hex"])).hexdigest()
    assert projected["byte_count"] == len(bytes.fromhex(image["raw_hex"]))
    assert projected["shape_tczyxs"] == [1, 1, 2, 2, 2, 1]


@pytest.mark.parametrize("mutation", ("missing_raw", "truncated", "storage", "spacing_bits", "scalar_metadata", "nonfinite_f32"))
def test_actual_snapshot_requires_complete_raw_and_scalar_transport(mutation):
    kind = "f32" if mutation == "nonfinite_f32" else "u8"
    image = artificial_snapshot(kind)
    if mutation == "missing_raw": del image["raw_hex"]
    elif mutation == "truncated": image["raw_hex"] = image["raw_hex"][:-2]
    elif mutation == "storage": image["storage_raw_hex"] = "00" * 8
    elif mutation == "spacing_bits": image["scalar_calibration"]["spacing_float64_raw_hex"][0] = "0000000000000000"
    elif mutation == "scalar_metadata": del image["scalar_calibration"]["origin_pixels"]
    else: image["raw_hex"] = image["storage_raw_hex"] = (struct.pack("<f", math.inf) * 8).hex()
    with pytest.raises(ValueError): fixtures._slicer_snapshot(image, kind, "artificial")


def test_missing_java_roles_leave_capture_gap_without_credit(tmp_path):
    result = {"capture_gaps": []}
    assert fixtures._validate_capture(tmp_path, {"id": "F.synthetic"}, {"id": "FC.synthetic", "format": "imagej_slicer_bilinear_v1", "capture": {}}, [], None, result) is False
    assert {row["kind"] for row in result["capture_gaps"]} >= {"runtime", "driver", "raw_output"}


@pytest.mark.parametrize("value", ("file://remote/C:/driver/", "file:/C:/driver/../other/", "https://invalid/driver/", "file:/relative/"))
def test_java_codesource_refuses_remote_alias_or_relative_origin(value):
    with pytest.raises(ValueError): fixtures._slicer_file_url(value, "artificial origin")


def test_literal_java_codesource_is_readonly_windows_origin():
    assert str(fixtures._slicer_file_url("file:/C:/root/driver-classes/", "artificial")) == "C:\\root\\driver-classes"


@pytest.mark.parametrize("kind", ("owned_synthetic", "python", "unknown"))
def test_upstream_format_cannot_be_relabelled_owned_or_nonjava_runtime(tmp_path, kind):
    capture = {name: {} for name in ("receipt", "raw_output", "generator_sources", "reference_archive", "reference_members", "driver")}
    capture["runtime"] = {"kind": kind}
    with pytest.raises(ValueError, match="genuine Java"):
        fixtures._validate_capture(tmp_path, {"id": "F.synthetic"},
            {"id": "FC.synthetic", "format": "imagej_slicer_bilinear_v1", "capture": capture}, [], None, {"capture_gaps": []})


def test_java_format_requires_pinned_external_driver_without_literal_fallback(tmp_path):
    capture = {name: {} for name in ("receipt", "raw_output", "generator_sources", "reference_archive", "reference_members")}
    capture.update(runtime={"kind": "java"}, driver=None)
    with pytest.raises(ValueError, match="external Java driver"):
        fixtures._validate_capture(tmp_path, {"id": "F.synthetic"},
            {"id": "FC.synthetic", "format": "imagej_slicer_bilinear_v1", "capture": capture}, [], None, {"capture_gaps": []})


def test_original_input_annex_cannot_use_unobserved_role_aliases():
    original = {"schema_version": 1, "state": "observed240_public_slicer_bilinear_reference_transport_only",
        "case_count": 240, "case_ids": list(IDS), "bounds": fixtures.SLICER_BOUNDS,
        "declaration": fixtures.SLICER_DECLARATION, "actual_runtime_origins_checked_rows": 240,
        "reference_runtime_observed": True, "reference_pixels_or_calibration_validated": False,
        "target_executed": False, "expected_pixels_generated": False,
        "inputs": {"script": fixtures.SLICER_GENERATOR, "driver": fixtures.SLICER_DRIVER,
            "recipe": fixtures.SLICER_RECIPE, "discovery": fixtures.ORIENTATION_DISCOVERY,
            "commercial": fixtures.ORIENTATION_COMMERCIAL_REVIEW,
            "qualified_transport_helper": fixtures.ORIENTATION_GENERATOR}}
    with pytest.raises(ValueError, match="exact original capture input roles"):
        fixtures._slicer_original_identity(original, "artificial role aliases")
