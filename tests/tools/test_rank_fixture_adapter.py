"""Independent transport/guard assertions for the retained finite rank fixture.

The literals below are retained ImageJ bytes, not calculated filter outputs.
These tests add no captures, product math, editor proof or numerical runs.  The
portable corpus has 161 selected runs and ten separately validated footprints.
"""

from collections import Counter
from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/fixtures/rank_imagej_154r_001.json"
FIXTURE_BYTES = 311070
FIXTURE_SHA256 = "121d1688e9a26c20818fbebfe88cbc4d1704ad26d330bfff5bdcc4d8e11694e5"
FORMAT = "imagej_rank_filter_finite_scalar_v1"
INPUT_FIELDS = (
    "id", "source_case_id", "source_observation_sha256", "dtype",
    "shape_tczyx", "position_tcz", "roi", "dispatch", "all_stack", "method", "radius",
)
EXPECTED_FIELDS = ("id", "dtype", "shape_tczyx", "position_tcz")
KERNEL_IDS = (
    "K01/0.0", "K01/0.5", "K01/1.0", "K01/1.5", "K01/1.75",
    "K01/2.0", "K01/2.5", "K01/2.85", "K01/3.0", "K01/10.0",
)
ORDERED_TCZ = [
    [0, 0, 0], [0, 1, 0], [0, 2, 0], [0, 0, 1], [0, 1, 1], [0, 2, 1],
    [0, 0, 2], [0, 1, 2], [0, 2, 2], [0, 0, 3], [0, 1, 3], [0, 2, 3],
    [1, 0, 0], [1, 1, 0], [1, 2, 0], [1, 0, 1], [1, 1, 1], [1, 2, 1],
    [1, 0, 2], [1, 1, 2], [1, 2, 2], [1, 0, 3], [1, 1, 3], [1, 2, 3],
]


def fingerprint(value):
    # Deliberately independent of the adapter's serializer/hash helpers.
    wire = json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode("utf-8")
    return hashlib.sha256(wire).hexdigest()


def independent_projection(row, locator):
    source = {field: deepcopy(row[field]) for field in INPUT_FIELDS}
    source["planes"] = [{"tcz": deepcopy(plane["tcz"]), "before_hex": plane["before_hex"]}
                        for plane in row["planes"]]
    expected = {field: deepcopy(row[field]) for field in EXPECTED_FIELDS}
    expected["planes"] = [{"tcz": deepcopy(plane["tcz"]), "after_hex": plane["after_hex"]}
                          for plane in row["planes"]]
    return {"locator": locator, "input": source, "expected": expected}


@pytest.fixture(scope="module")
def adapter():
    specification = importlib.util.spec_from_file_location(
        "rank_fixture_adapter_under_test", ROOT / "scripts/parity_fixtures.py")
    module = importlib.util.module_from_spec(specification)
    previous_bytecode = sys.dont_write_bytecode
    try:
        sys.dont_write_bytecode = True
        specification.loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = previous_bytecode
    return module


@pytest.fixture
def corpus():
    payload = FIXTURE.read_bytes()
    assert len(payload) == FIXTURE_BYTES
    assert hashlib.sha256(payload).hexdigest() == FIXTURE_SHA256
    return json.loads(payload)


def test_rfa01_exact_portable_membership_is_161_runs_and_ten_kernels(corpus):
    assert corpus["schema_version"] == 1
    assert corpus["kind"] == "retained_imagej_native_rank_fixture"
    assert corpus["imagej_version"] == "1.54r"
    assert corpus["imagej_commit"] == "62db90a269cccc1ac79f658e4602043214ed055b"
    assert corpus["run_count"] == len(corpus["runs"]) == 161
    assert corpus["original_selected_run_count"] == 158
    assert corpus["supplement_run_count"] == 3
    assert corpus["kernel_count"] == len(corpus["kernels"]) == 10
    assert tuple(row["id"] for row in corpus["kernels"]) == KERNEL_IDS
    assert len({row["id"] for row in corpus["runs"]}) == 161
    assert Counter(row["source_case_id"] for row in corpus["runs"]) == {
        "K02": 3, "K03": 3, "K04": 3, "K05": 3, "K06": 8, "K07": 1,
        "K08": 1, "K09": 108, "K10": 8, "K11": 12, "K14": 8, "SZ01": 3,
    }
    # The original archive's 174 requests include excluded profiles.  Neither
    # those nor the ten geometry records become extra portable filter cases.
    assert corpus["provenance"]["original_numerical_run_count"] == 174
    assert corpus["scope"] == (
        "Core numerical/rectangle API only; no stack dispatch, mask restoration, "
        "editor, commercial, or platform acceptance")


def test_rfa02_all_projections_and_hashes_preserve_every_ordered_field(adapter, corpus):
    before = fingerprint(corpus)
    actual = adapter.project_cases(FORMAT, corpus)
    expected = [independent_projection(row, index) for index, row in enumerate(corpus["runs"])]
    assert actual == expected
    assert [row["locator"] for row in actual] == list(range(161))
    assert len(actual) == 161
    for projected, independent in zip(actual, expected):
        assert fingerprint(projected["input"]) == fingerprint(independent["input"])
        assert fingerprint(projected["expected"]) == fingerprint(independent["expected"])
    assert fingerprint(corpus) == before
    assert not ({row["input"]["id"] for row in actual} & set(KERNEL_IDS))


# Each tuple pins one observed transport, including its original observation
# identity.  Width/height and Float32 byte order are deliberately asymmetric.
LITERAL_RUNS = (
    (35, "K09/uint8.1x7.minimum.1", "uint8", [1, 1, 1, 7, 1], "minimum", 1,
     "c5a4ec035cfa3a024efc24843f5a554031d08978e83c77153536b7fc010b068f",
     "01020304050607", "01010203040506",
     "32bbe378a25091502b2baf9f7258c19444e7a43ee4593b08030acd790bd66e6a",
     "8f3a3da151f9bab524b8d9652de12e532d0b0b9271167a3b43d5ce340bc16172"),
    (87, "K09/uint16.7x1.maximum.2", "uint16", [1, 1, 1, 1, 7], "maximum", 2,
     "32ef76c1a188f0aa52049412b0716b42a2f35680489ff447e07f93e2e8ba2745",
     "0100020003000400050006000700", "0300040005000600070007000700",
     "0656ee43f37a429628c19469b617f943f1ce308e856685d933e9c46ef07e54d4",
     "528f30bc5cc44cc5d8760ba0d9c08f98c08fef2a4dfbf1104a481b292b44138a"),
    (100, "K09/float32.1x1.median.0", "float32", [1, 1, 1, 1, 1], "median", 0,
     "b87127db030c16fda08ead9dfff609d0e984aa167d1c6aa1c729921ef65956f9",
     "0000803f", "0000803f",
     "e00e5eb9444182f352323374ef4e08ebcb784725fdd4fd612d7730540b3e0c8c",
     "e00e5eb9444182f352323374ef4e08ebcb784725fdd4fd612d7730540b3e0c8c"),
    (117, "K09/float32.1x7.mean.2", "float32", [1, 1, 1, 7, 1], "mean", 2,
     "fb1014c1b3b441ddef6db65066131f2054446a00fd839f0ca44c627a24da6193",
     "0000803f0000004000004040000080400000a0400000c0400000e040",
     "310cc33f9224094000004040000080400000a040b76dbb40f43ccf40",
     "81d04e55f282ee72ac60f77fc4a6773e346affab2f403935c3a785efdf990f47",
     "5d163f4bbb342dd6c48e5e73d296d1c72092a9c70fb5d12cdaa0ce3fb7deff4b"),
)


@pytest.mark.parametrize("sample", LITERAL_RUNS,
                         ids=("u8-minimum", "u16-maximum", "f32-median-zero-radius", "f32-mean"))
def test_rfa03_literal_typed_projection_and_raw_hash(adapter, corpus, sample):
    index, run_id, dtype, shape, method, radius, observation, before, after, before_sha, after_sha = sample
    assert adapter.project_cases(FORMAT, corpus)[index] == {
        "locator": index,
        "input": {"id": run_id, "source_case_id": "K09", "source_observation_sha256": observation,
                  "dtype": dtype, "shape_tczyx": shape, "position_tcz": [0, 0, 0],
                  "roi": None, "dispatch": "api", "all_stack": False, "method": method,
                  "radius": radius, "planes": [{"tcz": [0, 0, 0], "before_hex": before}]},
        "expected": {"id": run_id, "dtype": dtype, "shape_tczyx": shape,
                     "position_tcz": [0, 0, 0], "planes": [{"tcz": [0, 0, 0], "after_hex": after}]},
    }
    assert hashlib.sha256(bytes.fromhex(before)).hexdigest() == before_sha
    assert hashlib.sha256(bytes.fromhex(after)).hexdigest() == after_sha


@pytest.mark.parametrize("index,method,observation,before_sha,after_sha", (
    (158, "minimum", "b3f531da084ae8a13eaaaa646cec986a8643bc2e50bd62d8d6d36be262873d34",
     "8d12fac7f19291fb5f3ab47c45620c55d4ec08c01b8956199dffb9d0effa532f",
     "8232a0fa4f953a38c921e3c0304077dbdfbf9ebe36d9dbea285a6f982b1c7e3d"),
    (159, "maximum", "1c4e7d6074f5d3efef8457c9877371cbfd0bb208529cbd58432a1c13b20a440d",
     "8d12fac7f19291fb5f3ab47c45620c55d4ec08c01b8956199dffb9d0effa532f",
     "8232a0fa4f953a38c921e3c0304077dbdfbf9ebe36d9dbea285a6f982b1c7e3d"),
    (160, "median", "3cc43e9f2cc1a8160a124e4a6f9c1bb974dc9a3ffe3f55dd64f3dc8af09064f8",
     "9c1a56a1d4be8457f0bba13f9ea28f66ef4da3a07b6312147856bce6ab898343",
     "8d12fac7f19291fb5f3ab47c45620c55d4ec08c01b8956199dffb9d0effa532f"),
), ids=("minimum", "maximum", "median"))
def test_rfa04_signed_zero_bits_survive_without_float_normalization(
        adapter, corpus, index, method, observation, before_sha, after_sha):
    negative_before = {3, 12, 21, 30, 39, 48, 57}
    negative_after = negative_before if method == "median" else {2, 11, 20, 29, 38, 47, 56}
    before_words = [0x80000000 if index in negative_before else 0 for index in range(63)]
    if method == "median":
        before_words[31] = 0x3F800000
    after_words = [0x80000000 if index in negative_after else 0 for index in range(63)]
    before = struct.pack("<63I", *before_words)
    after = struct.pack("<63I", *after_words)
    row = adapter.project_cases(FORMAT, corpus)[index]
    assert row["input"]["id"] == row["expected"]["id"] == f"SZ01/{method}.radius2.signedzero"
    assert row["input"]["source_observation_sha256"] == observation
    assert row["input"]["dtype"] == row["expected"]["dtype"] == "float32"
    assert row["input"]["shape_tczyx"] == row["expected"]["shape_tczyx"] == [1, 1, 1, 7, 9]
    assert row["input"]["method"] == method and row["input"]["radius"] == 2
    assert row["input"]["planes"] == [{"tcz": [0, 0, 0], "before_hex": before.hex()}]
    assert row["expected"]["planes"] == [{"tcz": [0, 0, 0], "after_hex": after.hex()}]
    assert hashlib.sha256(before).hexdigest() == before_sha
    assert hashlib.sha256(after).hexdigest() == after_sha


@pytest.mark.parametrize("index,run_id,roi", (
    (130, "K10/rectangle.minimum", {"kind": "rectangle", "xywh": [1, 1, 7, 5]}),
    (137, "K10/hole.mean", {"kind": "holed_area", "outer_xywh": [1, 1, 7, 5],
                            "hole_xywh": [3, 2, 3, 3]}),
    (142, "K11/point.minimum", {"kind": "point", "xy": [2, 2]}),
    (146, "K11/line.minimum", {"kind": "line", "xyxy": [1, 1, 7, 5]}),
), ids=("rectangle", "holed-area", "point", "line"))
def test_rfa05_literal_runner_roi_is_preserved_without_mask_claim(adapter, corpus, index, run_id, roi):
    source = adapter.project_cases(FORMAT, corpus)[index]["input"]
    assert source["id"] == run_id and source["roi"] == roi
    assert source["shape_tczyx"] == [1, 1, 1, 7, 9]
    assert source["position_tcz"] == [0, 0, 0]
    assert source["dtype"] == "uint16" and source["radius"] == 1
    assert source["dispatch"] == "runner" and source["all_stack"] is False


@pytest.mark.parametrize("index,run_id,all_stack,changed", (
    (150, "K14/current.minimum", False, [[1, 2, 3]]),
    (154, "K14/all_stack.minimum", True, ORDERED_TCZ),
), ids=("current", "all-stack"))
def test_rfa06_imagej_ordered_planes_and_all_stack_flag_remain_distinct(
        adapter, corpus, index, run_id, all_stack, changed):
    projected = adapter.project_cases(FORMAT, corpus)[index]
    source, expected = projected["input"], projected["expected"]
    assert source["id"] == run_id and source["dispatch"] == "runner"
    assert source["all_stack"] is all_stack and source["method"] == "minimum"
    assert source["shape_tczyx"] == expected["shape_tczyx"] == [2, 3, 4, 7, 9]
    assert source["position_tcz"] == expected["position_tcz"] == [1, 2, 3]
    assert [plane["tcz"] for plane in source["planes"]] == ORDERED_TCZ
    assert [plane["tcz"] for plane in expected["planes"]] == ORDERED_TCZ
    assert [before["tcz"] for before, after in zip(source["planes"], expected["planes"])
            if before["before_hex"] != after["after_hex"]] == changed


def test_rfa07_literal_footprints_validate_without_becoming_extra_runs(adapter, corpus):
    cross = {"half_widths": [0, 1, 0], "point_count": 5,
             "offsets_xy": [[0, -1], [-1, 0], [0, 0], [1, 0], [0, 1]]}
    for index, radius in ((0, 0.0), (1, 0.5)):
        assert corpus["kernels"][index] == {"id": KERNEL_IDS[index], "radius": radius, **cross}
    assert corpus["kernels"][3]["half_widths"] == [0, 1, 2, 1, 0]
    assert corpus["kernels"][3]["point_count"] == 13
    assert corpus["kernels"][9]["half_widths"] == [
        1, 4, 6, 7, 8, 8, 9, 9, 9, 10, 10, 10, 9, 9, 9, 8, 8, 7, 6, 4, 1]
    assert corpus["kernels"][9]["point_count"] == len(corpus["kernels"][9]["offsets_xy"]) == 325
    assert len(adapter.project_cases(FORMAT, corpus)) == 161


def test_rfa08_projection_has_detached_mutable_fields(adapter, corpus):
    projected = adapter.project_cases(FORMAT, corpus)
    projected[0]["input"]["shape_tczyx"][0] = 2
    projected[0]["input"]["planes"][0]["tcz"][0] = 1
    projected[0]["expected"]["position_tcz"][0] = 1
    projected[130]["input"]["roi"]["xywh"][0] = 0
    assert corpus["runs"][0]["shape_tczyx"] == [1, 1, 1, 7, 9]
    assert corpus["runs"][0]["planes"][0]["tcz"] == [0, 0, 0]
    assert corpus["runs"][0]["position_tcz"] == [0, 0, 0]
    assert projected[0]["expected"]["shape_tczyx"] == [1, 1, 1, 7, 9]
    assert corpus["runs"][130]["roi"]["xywh"] == [1, 1, 7, 5]


def test_rfa09_legacy_owned_projection_and_existing_format_registry_are_preserved(adapter):
    assert {"imagej_binary_processor_v1", "imagej_conversion_processor_v1", "imagej_roi_style_v1",
            "imagej_roi_boolean_v1", "imagej_axis_plane_v1", "imagej_axis_range_v1",
            "imagej_axis_range_orientation_v1", "owned_cases_v1", "imagej_slicer_bilinear_v1",
            FORMAT} <= adapter.FORMATS
    old = {"schema_version": 1, "cases": [{"input": {"shape": [1, 2]}, "expected": {"pixels": [4, 5]}}]}
    assert adapter.project_cases("owned_cases_v1", old) == [
        {"locator": 0, "input": {"shape": [1, 2]}, "expected": {"pixels": [4, 5]}}]


def replace(corpus, path, value):
    target = corpus
    for part in path[:-1]:
        target = target[part]
    target[path[-1]] = deepcopy(value)


INVALID_FIELDS = (
    ("bool-schema", ("schema_version",), True),
    ("missing-run-count", ("run_count",), 160),
    ("bool-run-count", ("run_count",), True),
    ("wrong-kernel-count", ("kernel_count",), 11),
    ("original-selected-count", ("original_selected_run_count",), 161),
    ("supplement-count", ("supplement_run_count",), 0),
    ("unsupported-type", ("runs", 0, "dtype"), "float64"),
    ("bool-type", ("runs", 0, "dtype"), True),
    ("unsupported-method", ("runs", 0, "method"), "variance"),
    ("bool-radius", ("runs", 0, "radius"), True),
    ("nan-radius", ("runs", 0, "radius"), float("nan")),
    ("infinite-radius", ("runs", 0, "radius"), float("inf")),
    ("negative-radius", ("runs", 0, "radius"), -1),
    ("minimum-radius-bound", ("runs", 0, "radius"), 1001),
    ("median-radius-bound", ("runs", 100, "radius"), 101),
    ("numeric-all-stack", ("runs", 0, "all_stack"), 1),
    ("string-all-stack", ("runs", 0, "all_stack"), "false"),
    ("invalid-dispatch", ("runs", 0, "dispatch"), "desktop"),
    ("short-shape", ("runs", 0, "shape_tczyx"), [1, 1, 7, 9]),
    ("bool-shape", ("runs", 0, "shape_tczyx", 0), True),
    ("zero-shape", ("runs", 0, "shape_tczyx", 3), 0),
    ("fractional-shape", ("runs", 0, "shape_tczyx", 4), 9.0),
    ("bool-position", ("runs", 0, "position_tcz", 0), False),
    ("negative-position", ("runs", 0, "position_tcz", 1), -1),
    ("position-outside-shape", ("runs", 0, "position_tcz", 2), 1),
    ("short-position", ("runs", 0, "position_tcz"), [0, 0]),
    ("plane-outside-shape", ("runs", 0, "planes", 0, "tcz", 2), 1),
    ("bool-plane-position", ("runs", 0, "planes", 0, "tcz", 1), True),
    ("unknown-roi", ("runs", 130, "roi", "kind"), "polygon"),
    ("bool-roi-coordinate", ("runs", 130, "roi", "xywh", 0), True),
    ("roi-negative", ("runs", 130, "roi", "xywh", 0), -1),
    ("roi-outside-image", ("runs", 130, "roi", "xywh", 2), 10),
    ("roi-empty", ("runs", 130, "roi", "xywh", 3), 0),
    ("hole-outside-outer", ("runs", 137, "roi", "hole_xywh"), [0, 0, 1, 1]),
    ("point-outside-image", ("runs", 142, "roi", "xy", 0), 9),
    ("point-bool", ("runs", 142, "roi", "xy", 1), True),
    ("line-outside-image", ("runs", 146, "roi", "xyxy", 3), 7),
    ("line-bool", ("runs", 146, "roi", "xyxy", 0), False),
    ("non-string-hex", ("runs", 0, "planes", 0, "before_hex"), [0, 1]),
    ("odd-hex", ("runs", 0, "planes", 0, "before_hex"), "0"),
    ("invalid-hex", ("runs", 0, "planes", 0, "after_hex"), "zz" * 63),
    ("truncated-hex", ("runs", 0, "planes", 0, "before_hex"), "00" * 62),
    ("extra-hex-byte", ("runs", 0, "planes", 0, "after_hex"), "00" * 64),
    ("float-positive-infinity", ("runs", 100, "planes", 0, "before_hex"), "0000807f"),
    ("float-negative-infinity", ("runs", 100, "planes", 0, "after_hex"), "000080ff"),
    ("float-nan", ("runs", 100, "planes", 0, "after_hex"), "0000c07f"),
    ("imagej-version", ("imagej_version",), "1.54p"),
    ("imagej-commit", ("imagej_commit",), "0" * 40),
    ("reference-runtime-pin", ("reference", "runtime_archive_sha256"), "0" * 64),
    ("observation-pin", ("runs", 0, "source_observation_sha256"), "g" * 64),
    ("source-case", ("runs", 0, "source_case_id"), "K13"),
    ("raw-pin-size-bool", ("provenance", "raw", "bytes"), True),
    ("raw-pin-digest", ("provenance", "raw", "sha256"), "0" * 64),
    ("scope-promotion", ("scope",), "Full editor and commercial acceptance"),
    ("kernel-radius-bool", ("kernels", 0, "radius"), False),
    ("kernel-halfwidth-bool", ("kernels", 0, "half_widths", 0), False),
    ("kernel-halfwidth-negative", ("kernels", 0, "half_widths", 0), -1),
    ("kernel-pointcount-bool", ("kernels", 0, "point_count"), True),
    ("kernel-pointcount-mismatch", ("kernels", 0, "point_count"), 6),
    ("kernel-offset-nonintegral", ("kernels", 0, "offsets_xy", 0, 0), 0.5),
    ("kernel-offset-geometry", ("kernels", 0, "offsets_xy", 0, 0), 1),
)


@pytest.mark.parametrize("label,path,value", INVALID_FIELDS, ids=[row[0] for row in INVALID_FIELDS])
def test_rfa10_malformed_types_bits_geometry_and_provenance_refuse(adapter, corpus, label, path, value):
    replace(corpus, path, value)
    with pytest.raises(ValueError):
        adapter.project_cases(FORMAT, corpus)


MEMBERSHIP_MUTATIONS = (
    "missing-run", "duplicate-run", "reordered-runs", "duplicate-run-id", "missing-kernel",
    "duplicate-kernel", "reordered-kernels", "missing-plane", "duplicate-plane", "reordered-planes",
    "missing-input-field", "missing-before", "missing-after", "missing-provenance",
)


def mutate_membership(corpus, label):
    if label == "missing-run": corpus["runs"].pop()
    elif label == "duplicate-run": corpus["runs"][1] = deepcopy(corpus["runs"][0])
    elif label == "reordered-runs": corpus["runs"][0:2] = reversed(corpus["runs"][0:2])
    elif label == "duplicate-run-id": corpus["runs"][1]["id"] = corpus["runs"][0]["id"]
    elif label == "missing-kernel": corpus["kernels"].pop()
    elif label == "duplicate-kernel": corpus["kernels"][1] = deepcopy(corpus["kernels"][0])
    elif label == "reordered-kernels": corpus["kernels"][0:2] = reversed(corpus["kernels"][0:2])
    elif label == "missing-plane": corpus["runs"][150]["planes"].pop()
    elif label == "duplicate-plane": corpus["runs"][150]["planes"][1] = deepcopy(corpus["runs"][150]["planes"][0])
    elif label == "reordered-planes": corpus["runs"][150]["planes"][0:2] = reversed(corpus["runs"][150]["planes"][0:2])
    elif label == "missing-input-field": del corpus["runs"][0]["dispatch"]
    elif label == "missing-before": del corpus["runs"][0]["planes"][0]["before_hex"]
    elif label == "missing-after": del corpus["runs"][0]["planes"][0]["after_hex"]
    elif label == "missing-provenance": del corpus["provenance"]["supplement"]
    else: raise AssertionError(label)


@pytest.mark.parametrize("label", MEMBERSHIP_MUTATIONS, ids=MEMBERSHIP_MUTATIONS)
def test_rfa11_partial_duplicate_and_reordered_membership_refuses(adapter, corpus, label):
    mutate_membership(corpus, label)
    with pytest.raises(ValueError):
        adapter.project_cases(FORMAT, corpus)


BOUND_MUTATIONS = (
    "depth", "nodes", "dict-keys", "list-items", "string-length", "aggregate-strings",
    "run-count", "kernel-count", "shape-bound", "plane-count", "hex-length",
)


def mutate_bound(corpus, label):
    if label == "depth":
        nested = 0
        for unused in range(12): nested = [nested]
        corpus["extra"] = nested
    elif label == "nodes": corpus["extra"] = [[0] * 511 for unused in range(33)]
    elif label == "dict-keys": corpus["extra"] = {str(index): 0 for index in range(33)}
    elif label == "list-items": corpus["extra"] = [0] * 513
    elif label == "string-length": corpus["extra"] = "x" * 8193
    elif label == "aggregate-strings": corpus["extra"] = ["x" * 8192 for unused in range(32)]
    elif label == "run-count": corpus["runs"].append(deepcopy(corpus["runs"][0]))
    elif label == "kernel-count": corpus["kernels"].append(deepcopy(corpus["kernels"][0]))
    elif label == "shape-bound": corpus["runs"][0]["shape_tczyx"][3] = 26
    elif label == "plane-count": corpus["runs"][150]["planes"].append(deepcopy(corpus["runs"][150]["planes"][0]))
    elif label == "hex-length": corpus["runs"][0]["planes"][0]["before_hex"] = "00" * 64
    else: raise AssertionError(label)


@pytest.mark.parametrize("label", BOUND_MUTATIONS, ids=BOUND_MUTATIONS)
def test_rfa12_bounds_refuse_before_hex_decode_or_projection_copy(adapter, corpus, monkeypatch, label):
    mutate_bound(corpus, label)

    def forbidden(*args, **kwargs):
        raise AssertionError("Malformed bounded input reached hex decode or projection deepcopy")

    monkeypatch.setattr(adapter, "_rank_hex", forbidden)
    monkeypatch.setattr(adapter, "deepcopy", forbidden)
    with pytest.raises(ValueError):
        adapter.project_cases(FORMAT, corpus)


CAPTURE_ROLES = (
    "kind", "generator_sources", "raw_output", "input_plan", "execution_inputs",
    "verified_inputs", "supplement_output",
)
EXTRACTOR = {
    "path": "tests/reference/extract_rank_imagej_fixture.py", "bytes": 9716,
    "sha256": "d8bffc6c4082c0e42e1a676f893d81a91f62fbb4ed6f634bd19c1524fd5bc181",
}


def capture_contract(corpus):
    # These are declarations only. The tests below never open their retained
    # build/reference paths and never synthesize a successful original capture.
    provenance = corpus["provenance"]
    capture = {"kind": "retained_imagej_rank_selection_v1", "generator_sources": [deepcopy(EXTRACTOR)]}
    for role, original in (("raw_output", "raw"), ("input_plan", "input_plan"),
                           ("execution_inputs", "execution_inputs"), ("verified_inputs", "verified_inputs"),
                           ("supplement_output", "supplement")):
        capture[role] = deepcopy(provenance[original])
    return {"id": "FC.owned-rank-capture-guards", "format": FORMAT, "capture": capture}


@pytest.mark.parametrize("role", CAPTURE_ROLES, ids=CAPTURE_ROLES)
def test_rfa13_missing_capture_role_remains_a_gap_without_any_read(adapter, corpus, monkeypatch, role):
    contract = capture_contract(corpus)
    del contract["capture"][role]

    def forbidden(*args, **kwargs):
        raise AssertionError("Incomplete capture declaration attempted a retained-path read")

    monkeypatch.setattr(adapter, "_rank_capture_bytes", forbidden)
    result = {"capture_gaps": []}
    assert adapter._validate_capture(ROOT, {"id": "F.owned-rank-capture-guards"},
                                     contract, None, corpus, result) is False
    assert result["capture_gaps"]


@pytest.mark.parametrize("change", (
    "kind", "extra-role", "raw-sha", "plan-path", "verified-size", "generator",
), ids=("kind", "extra-role", "raw-sha", "plan-path", "verified-size", "generator"))
def test_rfa14_changed_capture_links_refuse_before_retained_path_reads(adapter, corpus, monkeypatch, change):
    contract = capture_contract(corpus)
    capture = contract["capture"]
    if change == "kind": capture["kind"] = "generic_java_capture_v1"
    elif change == "extra-role": capture["receipt"] = {"path": "owned.json", "sha256": "0" * 64}
    elif change == "raw-sha": capture["raw_output"]["sha256"] = "0" * 64
    elif change == "plan-path": capture["input_plan"]["path"] = "owned/aliased-plan.json"
    elif change == "verified-size": capture["verified_inputs"]["bytes"] += 1
    elif change == "generator": capture["generator_sources"][0]["path"] = "owned/other-extractor.py"

    def forbidden(*args, **kwargs):
        raise AssertionError("Changed capture declaration attempted a retained-path read")

    monkeypatch.setattr(adapter, "_rank_capture_bytes", forbidden)
    with pytest.raises(ValueError):
        adapter._validate_capture(ROOT, {"id": "F.owned-rank-capture-guards"},
                                  contract, None, corpus, {"capture_gaps": []})


def test_rfa15_unavailable_exact_capture_bytes_never_grant_capture_credit(adapter, corpus, monkeypatch):
    contract = capture_contract(corpus)
    observed = {}

    def unavailable(root, pin, where, result, kind, **identities):
        assert where == contract["id"]
        assert identities == {"fixture_id": "F.owned-rank-capture-guards", "contract_id": contract["id"]}
        observed[kind] = deepcopy(pin)
        return None

    monkeypatch.setattr(adapter, "_rank_capture_bytes", unavailable)
    result = {"capture_gaps": []}
    assert adapter._validate_capture(ROOT, {"id": "F.owned-rank-capture-guards"},
                                     contract, None, corpus, result) is False
    assert observed == {**{role: contract["capture"][role] for role in CAPTURE_ROLES[2:]},
                        "generator_source": EXTRACTOR}
    assert result["capture_gaps"]


@pytest.mark.parametrize("declared_size", (True, 0, -1, 1024 * 1024 + 1),
                         ids=("bool", "zero", "negative", "over-one-mib"))
def test_rfa16_capture_byte_bounds_refuse_before_path_access(adapter, monkeypatch, declared_size):
    def forbidden(*args, **kwargs):
        raise AssertionError("Invalid capture size reached filesystem path resolution")

    monkeypatch.setattr(adapter.evidence, "report_path", forbidden)
    pin = {"path": "owned.json", "bytes": declared_size, "sha256": "0" * 64}
    with pytest.raises(ValueError):
        adapter._rank_capture_bytes(ROOT, pin, "owned", {"unavailable_evidence": []}, "owned")


@pytest.mark.parametrize("mutation", ("size", "sha"), ids=("size", "sha"))
def test_rfa17_capture_file_size_and_hash_are_both_required(adapter, tmp_path, mutation):
    payload = b"Owned synthetic capture transport only.\n"
    (tmp_path / "owned.json").write_bytes(payload)
    pin = {"path": "owned.json", "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}
    if mutation == "size": pin["bytes"] -= 1
    else: pin["sha256"] = "0" * 64
    with pytest.raises(ValueError):
        adapter._rank_capture_bytes(tmp_path, pin, "owned", {"unavailable_evidence": []}, "owned")


def test_rfa18_capture_transport_reads_only_matching_owned_bytes_and_marks_absence(adapter, tmp_path):
    payload = b"Owned synthetic capture transport only.\n"
    (tmp_path / "owned.json").write_bytes(payload)
    pin = {"path": "owned.json", "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}
    result = {"unavailable_evidence": []}
    assert adapter._rank_capture_bytes(tmp_path, pin, "owned", result, "owned") == payload
    assert result["unavailable_evidence"] == []
    pin["path"] = "absent.json"
    assert adapter._rank_capture_bytes(tmp_path, pin, "owned", result, "owned") is None
    assert len(result["unavailable_evidence"]) == 1
    assert result["unavailable_evidence"][0]["path"] == "absent.json"
