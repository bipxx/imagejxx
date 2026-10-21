"""Independent finite ImageJ Gaussian profile and installed-SDK comparisons.

Required numerical gates configure IMAGEJXX_GAUSSIAN_REQUIRE_REFERENCE=1,
IMAGEJXX_GAUSSIAN_REFERENCE_JSONL/SHA256, IMAGEJXX_GAUSSIAN_CONTRACT_JSON/SHA256
and IMAGEJXX_GAUSSIAN_CPP_EXE. Routine suites explicitly opt out of the reference
tests when those tools are not configured. No SciPy/native output is an oracle.
"""

from collections import Counter
import hashlib
import inspect
import json
import math
import os
from pathlib import Path
import subprocess

import imagejxx as ij
import numpy as np
import pytest


COMMIT = "62db90a269cccc1ac79f658e4602043214ed055b"
ARCHIVE_SHA = "09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665"
GAUSSIAN_SHA = "2e5827a3e4b3aca0d2e43b9151be5cf2f3d1f69fb5d3e167bdd10d94e101b782"
DTYPES = {"U8": np.dtype("u1"), "U16": np.dtype("<u2"),
          "F32": np.dtype("<f4"), "RGB": np.dtype("u1")}
METHODS = {"direct", "runner-area-harness", "run-scaled-harness",
           "runner-nonarea-harness"}


def _sha(raw):
    return hashlib.sha256(raw).hexdigest()


def _pin(path):
    path = Path(path).resolve()
    return {"path": str(path), "sha256": _sha(path.read_bytes())}


def _no_constant(value):
    raise ValueError(f"Nonfinite JSON constant: {value}")


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON field: {key}")
        result[key] = value
    return result


def _json(raw):
    return json.loads(raw, parse_constant=_no_constant,
                      object_pairs_hook=_unique_object)


def _configured_pin(path_key, hash_key):
    configured = os.environ.get(path_key)
    expected = os.environ.get(hash_key)
    assert configured and expected, f"Set both {path_key} and {hash_key}"
    assert len(expected) == 64 and all(c in "0123456789abcdef" for c in expected)
    path = Path(configured).resolve()
    raw = path.read_bytes()
    assert _sha(raw) == expected, f"Pinned input bytes changed: {path}"
    return path, raw


def _required():
    value = os.environ.get("IMAGEJXX_GAUSSIAN_REQUIRE_REFERENCE", "0")
    assert value in ("0", "1"), "REQUIRE_REFERENCE must be exactly 0 or 1"
    return value == "1"


def _independent_input(case):
    height, width = case["shape"][:2]
    values = []
    for y in range(height):
        for x in range(width):
            base = (37*x + 61*y + 13*x*y + 19*(x % 3) + 23*(y % 5)) % 256
            if case["dtype"] == "U8":
                values.append((base + (47 if x == 0 else 0)
                               + (83 if y == height - 1 else 0)) % 256)
            elif case["dtype"] == "U16":
                values.append((257*base + 97*x + 211*y + 331*(x % 7)) % 65536)
            elif case["dtype"] == "F32":
                # Every term and intermediate here is an exactly representable
                # binary fraction at the declared geometry. Resolve once to f32.
                values.append((base - 127)*0.125 + (x - 2*y)*0.0625
                              + ((x*y) % 17)*0.00390625)
            else:
                assert case["dtype"] == "RGB"
                values.extend(((base + 3*x) % 256, (5*base + 11*y + 17) % 256,
                               (255 - base + 7*x + 19*y) % 256))
    return np.asarray(values, dtype=DTYPES[case["dtype"]]).reshape(case["shape"])


def _raw(array):
    return array.astype(array.dtype.newbyteorder("<"), copy=False).tobytes(order="C")


def _bounds(case):
    height, width = case["shape"][:2]
    roi = case["roi"]
    if not roi["is_area"]:
        return [0, 0, width, height]
    if "processor_bounds" in roi:
        return roi["processor_bounds"]
    return roi["requested_bounds"]


def _selection(case, *, mask=False):
    height, width = case["shape"][:2]
    x, y, extent_width, extent_height = _bounds(case)
    selected = np.zeros((height, width), dtype=bool)
    selected[y:y + extent_height, x:x + extent_width] = True
    if not mask:
        return selected
    kind = case["roi"]["kind"]
    if kind == "area-hole":
        hx, hy, hw, hh = case["roi"]["subtract_bounds"]
        selected[hy:hy + hh, hx:hx + hw] = False
    elif kind in ("irregular-l", "irregular-offcenter"):
        # The independent declared axis-aligned L is the union of its top bar
        # and left bar. Integer half-open edges match pixel-center membership.
        vertices = case["roi"]["vertices_xy"]
        cut_x, cut_y = vertices[3]
        selected[cut_y:y + extent_height, cut_x:x + extent_width] = False
    return selected


def _array_from_raw(case, raw):
    expected = math.prod(case["shape"]) * DTYPES[case["dtype"]].itemsize
    assert len(raw) == expected, f"Raw byte length mismatch: {case['id']}"
    return np.frombuffer(raw, dtype=DTYPES[case["dtype"]]).reshape(case["shape"])


def _composed_expected(case, record, *, rectangle=False):
    use_pre_mask = rectangle and bool(record["roi"]["mask_pixels"])
    key = "pre_mask_raw_hex" if use_pre_mask else "output_raw_hex"
    reference = _array_from_raw(case, bytes.fromhex(record[key]))
    source = _independent_input(case)
    selected = _selection(case, mask=not rectangle and record["mask_restoration_applied"])
    expected = source.copy()
    expected[selected] = reference[selected]
    return _raw(expected)


def _outside_changed(case, raw, *, mask=False):
    source = _raw(_independent_input(case))
    height, width = case["shape"][:2]
    pixel_bytes = len(source) // (height * width)
    a = np.frombuffer(raw, dtype=np.uint8).reshape((height, width, pixel_bytes))
    b = np.frombuffer(source, dtype=np.uint8).reshape((height, width, pixel_bytes))
    exterior = ~_selection(case, mask=mask)
    return int(np.count_nonzero(np.any(a != b, axis=2) & exterior))


def _outside_details(case, raw, *, mask=False):
    source = _raw(_independent_input(case))
    height, width = case["shape"][:2]
    pixel_bytes = len(source) // (height * width)
    exterior = ~_selection(case, mask=mask)
    result = []
    for y in range(height):
        for x in range(width):
            offset = (y*width + x) * pixel_bytes
            before, after = source[offset:offset + pixel_bytes], raw[offset:offset + pixel_bytes]
            if exterior[y, x] and before != after:
                result.append({"coordinate_xy": [x, y], "input_raw_hex": before.hex(),
                               "output_raw_hex": after.hex()})
    return result


@pytest.fixture(scope="module")
def gaussian_reference():
    if not _required() and not os.environ.get("IMAGEJXX_GAUSSIAN_REFERENCE_JSONL"):
        pytest.skip("Explicit opt-out: independent Gaussian reference gate is not configured")
    reference_path, raw = _configured_pin("IMAGEJXX_GAUSSIAN_REFERENCE_JSONL",
                                          "IMAGEJXX_GAUSSIAN_REFERENCE_SHA256")
    contract_path, contract_raw = _configured_pin("IMAGEJXX_GAUSSIAN_CONTRACT_JSON",
                                                  "IMAGEJXX_GAUSSIAN_CONTRACT_SHA256")
    contract = _json(contract_raw)
    comparison = contract["comparison"]
    assert comparison["expected_cases"] == 332
    assert comparison["expected_owned_output_policy"] == "reference-inside-write-extent/source-outside"
    assert comparison["reference_exterior_leakage"] == "preserve-and-report"
    declaration_pin = comparison["case_declaration"]
    declaration_path = Path(declaration_pin["path"])
    if not declaration_path.is_absolute():
        declaration_path = Path(__file__).resolve().parents[2] / declaration_path
    declaration_raw = declaration_path.read_bytes()
    assert _sha(declaration_raw) == declaration_pin["sha256"]
    declaration = _json(declaration_raw)
    cases = declaration["cases"]
    assert declaration["expected_records"] == len(cases) == 332
    assert len({case["id"] for case in cases}) == 332
    assert Counter(case["group"] for case in cases) == declaration["group_counts"]
    lines = raw.decode("utf-8").split("\n")
    assert lines[-1] == "", "Reference JSONL must have a final newline"
    records = [_json(value) for value in lines[:-1]]
    assert [row["id"] for row in records] == [case["id"] for case in cases]
    reference_identity = records[0]["reference"]
    assert reference_identity["version"] == "1.54r"
    assert reference_identity["commit"] == COMMIT
    assert reference_identity["archive_sha256"] == ARCHIVE_SHA
    assert reference_identity["source_members"]["ij/plugin/filter/GaussianBlur.java"] == GAUSSIAN_SHA
    assert reference_identity["runtime_class_sha256"]
    for member, source_hash in reference_identity["source_members"].items():
        assert source_hash == declaration["reference"]["source_members"][member]
    assert all(len(value) == 64 and all(c in "0123456789abcdef" for c in value)
               for value in reference_identity["runtime_class_sha256"].values())
    for case, record in zip(cases, records, strict=True):
        assert record["reference"] == reference_identity
        for key in ("dtype", "shape", "sigma_xy", "scalar_sigma", "scaled_units",
                    "pixel_spacing_xy", "calibration_unit", "accuracy", "border",
                    "method", "raw_encoding", "equivalent_to", "direct_kernel_peer",
                    "related_full_plane_id"):
            assert record[key] == case[key], f"Declared {key} differs: {case['id']}"
        assert record["method"] in METHODS
        assert record["direction_order"] == ["X", "Y"]
        assert record["full_plugin_runner_executed"] is False
        assert record["dialog_executed"] is False
        assert record["runner_preparation_harness"] is (case["method"] != "direct")
        assert record["source_preserved"] is True
        assert record["raw_reference_output_unmodified"] is True
        assert record["roi"]["kind"] == case["roi"]["kind"]
        assert record["roi"]["is_area"] is case["roi"]["is_area"]
        assert record["roi"]["processor_bounds"] == _bounds(case)
        source_raw = bytes.fromhex(record["input_raw_hex"])
        assert source_raw == _raw(_independent_input(case))
        assert _sha(source_raw) == record["input_sha256"]
        output_raw = bytes.fromhex(record["output_raw_hex"])
        observed = _array_from_raw(case, output_raw)
        assert np.all(np.isfinite(observed))
        assert _sha(output_raw) == record["output_sha256"]
        mask_values = record["roi"]["mask_pixels"]
        has_mask = case["roi"]["kind"] in ("area-hole", "irregular-l", "irregular-offcenter")
        assert bool(mask_values) is has_mask
        assert record["mask_restoration_applied"] is (has_mask and case["method"] != "direct")
        if has_mask:
            x, y, width, height = _bounds(case)
            assert record["roi"]["mask_shape"] == [height, width]
            expected_mask = _selection(case, mask=True)[y:y + height, x:x + width]
            assert all(type(value) is int and value in (0, 255) for value in mask_values)
            assert mask_values == (expected_mask.astype(np.uint8) * 255).ravel().tolist()
            pre_mask_raw = bytes.fromhex(record["pre_mask_raw_hex"])
            assert _sha(pre_mask_raw) == record["pre_mask_sha256"]
            assert np.all(np.isfinite(_array_from_raw(case, pre_mask_raw)))
        if case["roi"]["is_area"]:
            leaked = _outside_changed(case, output_raw, mask=record["mask_restoration_applied"])
            assert record["outside_changed_pixels"] == leaked
            assert record["outside_preserved"] is (leaked == 0)
            selected = _selection(case, mask=record["mask_restoration_applied"])
            assert record["outside_checked_pixels"] == selected.size - int(np.count_nonzero(selected))
            details = [{key: row[key] for key in ("coordinate_xy", "input_raw_hex", "output_raw_hex")}
                       for row in record["outside_changes"]]
            assert details == _outside_details(case, output_raw,
                                               mask=record["mask_restoration_applied"])
            assert record["outside_qualification"] == (
                "raw_reference_outside_writes_observed" if leaked else
                "raw_reference_exterior_unchanged_for_this_case")
        else:
            assert record["outside_preserved"] is None
            assert record["outside_changes"] == []
            assert record["outside_qualification"] == "not_applicable"
    return {"cases": cases, "records": records, "reference_path": reference_path,
            "reference_sha256": _sha(raw), "contract_path": contract_path,
            "contract_sha256": _sha(contract_raw), "declaration_pin": _pin(declaration_path),
            "groups": declaration["group_counts"], "reference": reference_identity}


def _assert_exact(case, actual, expected, label):
    if actual != expected:
        first = next((i for i, (a, b) in enumerate(zip(actual, expected)) if a != b),
                     min(len(actual), len(expected)))
        pytest.fail(f"{case['id']} {label}: exact raw mismatch at byte {first}; "
                    f"actual={actual[first:first+8].hex()} expected={expected[first:first+8].hex()}")


def _observe(case, record, kernel_raw):
    expected_rectangle = _composed_expected(case, record, rectangle=True)
    _assert_exact(case, kernel_raw, expected_rectangle, "native rectangle")
    assert _outside_changed(case, kernel_raw) == 0
    observed = _array_from_raw(case, kernel_raw).copy()
    if record["mask_restoration_applied"]:
        # Explicit test harness only: the numerical API owns rectangle writes,
        # while runner irregular restoration is a separate future executor task.
        source = _independent_input(case)
        observed[~_selection(case, mask=True)] = source[~_selection(case, mask=True)]
    observed_raw = _raw(observed)
    expected = _composed_expected(case, record)
    _assert_exact(case, observed_raw, expected, "declared after-mask harness")
    assert _outside_changed(case, observed_raw, mask=record["mask_restoration_applied"]) == 0
    reference_raw = bytes.fromhex(record["output_raw_hex"])
    reference_rectangle = bytes.fromhex(record["pre_mask_raw_hex"]
                                         if record["roi"]["mask_pixels"]
                                         else record["output_raw_hex"])
    raw_match = observed_raw == reference_raw
    rectangle_match = kernel_raw == reference_rectangle
    assert raw_match is (_outside_changed(case, reference_raw,
                                          mask=record["mask_restoration_applied"]) == 0)
    assert rectangle_match is (_outside_changed(case, reference_rectangle) == 0)
    return {"id": case["id"], "dtype": case["dtype"], "shape": case["shape"],
            "method": case["method"], "sigma_xy": case["sigma_xy"],
            "extent_xywh": _bounds(case) if case["roi"]["is_area"] else None,
            "input_sha256": record["input_sha256"], "source_preserved": True,
            "kernel_output_raw_hex": kernel_raw.hex(), "kernel_output_sha256": _sha(kernel_raw),
            "harness_mask_restoration": record["mask_restoration_applied"],
            "observed_output_raw_hex": observed_raw.hex(), "observed_output_sha256": _sha(observed_raw),
            "expected_owned_sha256": _sha(expected), "owned_composed_match": True,
            "owned_exterior_preserved": True, "raw_reference_full_match": raw_match,
            "raw_reference_rectangle_match": rectangle_match,
            "reference_output_sha256": record["output_sha256"],
            "reference_exterior_changed_pixels": _outside_changed(
                case, reference_raw, mask=record["mask_restoration_applied"]),
            "full_plugin_runner_executed": False, "dialog_executed": False}


def _write_report(tmp_path, context, corpus, rows, *, executable=None, protocol=None):
    base = Path(os.environ.get("IMAGEJXX_GAUSSIAN_REPORT_DIR", tmp_path)).resolve()
    base.mkdir(parents=True, exist_ok=True)
    report = {"schema_version": 1, "kind": "gaussian_numeric_observation",
              "context": context, "contract": {"path": str(corpus["contract_path"]),
                                               "sha256": corpus["contract_sha256"]},
              "reference_output": {"path": str(corpus["reference_path"]),
                                   "sha256": corpus["reference_sha256"]},
              "case_declaration": corpus["declaration_pin"],
              "groups": corpus["groups"], "observed_cases": len(rows),
              "owned_numerical_checkpoint_only": True,
              "full_command_or_ui_qualified": False,
              "full_raw_reference_match": all(row["raw_reference_full_match"] for row in rows),
              "reference_identity": corpus["reference"], "observations": rows,
              "source_artifacts": [_pin(__file__)],
              "runtime_artifacts": [_pin(executable)] if executable else
                  [_pin(ij._native.__file__), _pin(ij.__file__)]}
    if protocol is not None:
        request_raw, response_raw = protocol
        for name, raw in (("request", request_raw), ("response", response_raw)):
            path = base / f"{context}-{name}.txt"
            with path.open("xb") as stream:
                stream.write(raw)
            report[f"protocol_{name}"] = _pin(path)
    with (base / f"{context}.json").open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(report, stream, ensure_ascii=False, allow_nan=False, indent=2)
        stream.write("\n")


def test_gaussian_imagej_python_matches_all_reference_cases(gaussian_reference, tmp_path):
    corpus = gaussian_reference
    rows = []
    for case, record in zip(corpus["cases"], corpus["records"], strict=True):
        source = _independent_input(case)
        source_raw = _raw(source)
        sx, sy = case["sigma_xy"]
        extent = tuple(_bounds(case)) if case["roi"]["is_area"] else None
        if case["group"] == "full-plane" and sx == sy == 2:
            result = ij.gaussian_blur_imagej(source)
        else:
            result = ij.gaussian_blur_imagej(source, (sy, sx), extent=extent)
        assert result.shape == source.shape and result.dtype == source.dtype
        assert not np.shares_memory(source, result)
        assert _raw(source) == source_raw == bytes.fromhex(record["input_raw_hex"])
        rows.append(_observe(case, record, _raw(result)))
    assert len(rows) == 332
    _write_report(tmp_path, "python-api", corpus, rows)


@pytest.fixture(scope="module")
def gaussian_cpp_executable():
    configured = os.environ.get("IMAGEJXX_GAUSSIAN_CPP_EXE")
    if not configured and not _required():
        pytest.skip("Explicit opt-out: installed Gaussian SDK consumer is not configured")
    assert configured, "Required Gaussian gate needs IMAGEJXX_GAUSSIAN_CPP_EXE"
    executable = Path(configured).resolve()
    assert executable.is_file(), f"Installed SDK executable missing: {executable}"
    return executable


def _protocol_request(cases):
    lines = [f"IMAGEJXX_GAUSSIAN_V1 {len(cases)}"]
    for case in cases:
        source = _independent_input(case)
        height, width = case["shape"][:2]
        channels = 3 if case["dtype"] == "RGB" else 1
        sx, sy = case["sigma_xy"]
        extent = _bounds(case) if case["roi"]["is_area"] else [0, 0, 0, 0]
        tokens = ["CASE", case["id"], case["dtype"], width, height, channels,
                  format(sx, ".17g"), format(sy, ".17g"), int(case["roi"]["is_area"]),
                  *extent, source.size]
        lines.append(" ".join(map(str, tokens)))
        lines.append(" ".join(format(float(value), ".17g") for value in source.ravel()))
    return ("\n".join(lines) + "\n").encode("ascii")


def _protocol_results(cases, raw):
    lines = raw.decode("ascii").splitlines()
    assert lines[0] == f"IMAGEJXX_GAUSSIAN_RESULTS_V1 {len(cases)}"
    assert len(lines) == 1 + 4 * len(cases)
    results = []
    for index, case in enumerate(cases):
        header, source_line, output_line, end = lines[1 + 4*index:5 + 4*index]
        fields = header.split()
        height, width = case["shape"][:2]
        channels = 3 if case["dtype"] == "RGB" else 1
        assert fields == ["RESULT", case["id"], case["dtype"], str(width), str(height),
                          str(channels), str(math.prod(case["shape"])), "1"]
        assert source_line.startswith("INPUT ") and output_line.startswith("OUTPUT ")
        assert end == "END_RESULT"
        source_raw = bytes.fromhex(source_line.removeprefix("INPUT "))
        output_raw = bytes.fromhex(output_line.removeprefix("OUTPUT "))
        assert source_raw == _raw(_independent_input(case))
        _array_from_raw(case, output_raw)
        results.append(output_raw)
    return results


def test_gaussian_imagej_installed_cpp_matches_all_reference_cases(
        gaussian_reference, gaussian_cpp_executable, tmp_path):
    corpus = gaussian_reference
    request = _protocol_request(corpus["cases"])
    completed = subprocess.run([str(gaussian_cpp_executable), "--protocol"],
                               input=request, capture_output=True, timeout=180, check=False)
    assert completed.returncode == 0, completed.stderr.decode("utf-8", errors="replace")
    assert completed.stderr == b""
    results = _protocol_results(corpus["cases"], completed.stdout)
    rows = [_observe(case, record, observed) for case, record, observed in
            zip(corpus["cases"], corpus["records"], results, strict=True)]
    assert len(rows) == 332
    _write_report(tmp_path, "cpp-api", corpus, rows, executable=gaussian_cpp_executable,
                  protocol=(request, completed.stdout))


def test_gaussian_imagej_zero_sigma_bits_and_array_views():
    for source in (np.array([[-0.0, 0.0, -1.5, 2.25]], dtype=np.float32),
                   np.arange(35, dtype=np.uint16).reshape(5, 7)[:, ::-1],
                   np.arange(12, dtype=np.uint8).reshape(2, 2, 3)):
        source.flags.writeable = False
        before = _raw(source)
        result = ij.gaussian_blur_imagej(source, 0)
        assert result.shape == source.shape and result.dtype == source.dtype
        assert _raw(result) == before == _raw(source)
        assert not np.shares_memory(result, source)


def test_gaussian_imagej_empty_extent_and_public_defaults():
    source = np.arange(35, dtype=np.uint16).reshape(5, 7)
    before = source.tobytes()
    for extent in ((7, 5, 0, 0), (2, 1, 0, 3), (2, 1, 3, 0)):
        result = ij.gaussian_blur_imagej(source, 12, extent=extent)
        assert result.tobytes() == before == source.tobytes()
        assert not np.shares_memory(result, source)
    parameters = inspect.signature(ij.gaussian_blur_imagej).parameters
    assert parameters["sigma"].default == 2.0 and parameters["extent"].default is None
    generic = inspect.signature(ij.gaussian_blur).parameters
    assert generic["sigma"].default == 1 and generic["border"].default == "reflect"


@pytest.mark.parametrize("sigma", [-1, math.inf, -math.inf, math.nan, 1e300,
                                  (2, -1), (math.nan, 2), (2, math.inf),
                                  (), (1,), (1, 2, 3), True, np.bool_(False),
                                  (True, 2), (2, np.bool_(False))])
def test_gaussian_imagej_rejects_invalid_sigma_atomically(sigma):
    source = np.arange(35, dtype=np.uint8).reshape(5, 7)
    before = source.tobytes()
    with pytest.raises((TypeError, ValueError, OverflowError)):
        ij.gaussian_blur_imagej(source, sigma)
    assert source.tobytes() == before


@pytest.mark.parametrize("extent", [(-1, 0, 1, 1), (0, -1, 1, 1),
                                   (0, 0, -1, 1), (0, 0, 1, -1),
                                   (7, 0, 1, 1), (0, 5, 1, 1),
                                   (1, 1, 2**31 - 1, 1), (2**31, 0, 0, 0),
                                   (0, 0, 1.5, 1), (False, 0, 1, 1),
                                   (0, np.bool_(False), 1, 1), (), (0, 0, 1)])
def test_gaussian_imagej_rejects_invalid_extent_atomically(extent):
    source = np.arange(35, dtype=np.uint8).reshape(5, 7)
    before = source.tobytes()
    with pytest.raises((TypeError, ValueError, OverflowError)):
        ij.gaussian_blur_imagej(source, extent=extent)
    assert source.tobytes() == before


def test_gaussian_imagej_extent_uses_integer_index_protocol():
    class Index:
        def __init__(self, value):
            self.value = value

        def __index__(self):
            return self.value

    source = np.arange(35, dtype=np.uint8).reshape(5, 7)
    extent = (Index(2), np.int64(1), np.int32(3), Index(2))
    result = ij.gaussian_blur_imagej(source, 0, extent=extent)
    assert result.tobytes() == source.tobytes()


@pytest.mark.parametrize("source", [np.zeros((2, 2), dtype=np.float64),
                                   np.zeros((2, 2, 4), dtype=np.uint8),
                                   np.zeros((2, 2, 3), dtype=np.uint16),
                                   np.zeros((2, 2, 3), dtype=np.float32),
                                   np.zeros((0, 2), dtype=np.uint8),
                                   np.zeros((2, 2), dtype=np.int16),
                                   np.array([[math.inf]], dtype=np.float32),
                                   np.array([[math.nan]], dtype=np.float32)])
def test_gaussian_imagej_rejects_unsupported_or_nonfinite_pixels(source):
    before = source.tobytes()
    with pytest.raises((TypeError, ValueError, OverflowError)):
        ij.gaussian_blur_imagej(source, 0)
    assert source.tobytes() == before


@pytest.mark.parametrize("protocol_request", [
    "UNKNOWN 1\n",
    "IMAGEJXX_GAUSSIAN_V1 1\nCASE one U8 2 2 1 0 0 2 0 0 0 0 4\n0 1 2 3\n",
    "IMAGEJXX_GAUSSIAN_V1 1\nCASE one U8 2 2 1 0 0 0 0 0 0 0 4\n0 1 2\n",
    "IMAGEJXX_GAUSSIAN_V1 1\nCASE one U8 2 2 1 0 0 0 0 0 0 0 4\n0 1 2 3\nTRAILING\n",
    "IMAGEJXX_GAUSSIAN_V1 2\nCASE one U8 1 1 1 0 0 0 0 0 0 0 1\n0\n"
    "CASE one U8 1 1 1 0 0 0 0 0 0 0 1\n0\n",
])
def test_gaussian_imagej_installed_cpp_rejects_malformed_protocol(
        gaussian_cpp_executable, protocol_request):
    completed = subprocess.run([str(gaussian_cpp_executable), "--protocol"],
                               input=protocol_request.encode("ascii"), capture_output=True,
                               timeout=15, check=False)
    assert completed.returncode != 0 and completed.stderr
