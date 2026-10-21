"""Observe the installed public rank API from a bounded input-only envelope.

This script never reads the reference fixture or expected pixels, imports a
parity adapter, publishes an edit, or implements a filtering algorithm. A
separate reader compares its actual values with retained reference output.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import math
from pathlib import Path
import re
import struct
import sys
import time


MAX_INPUT_BYTES = 1024 * 1024
MAX_OUTPUT_BYTES = 16 * 1024 * 1024
METHODS = ("minimum", "maximum", "median", "mean")
DTYPES = ("uint8", "uint16", "float32")
RADII = (0.0, 0.5, 1.0, 1.5, 1.75, 2.0, 2.5, 2.85, 3.0, 10.0)
RUN_IDS = (
    *(f"K{case:02d}/{dtype}" for case in range(2, 6) for dtype in DTYPES),
    *(f"K06/radius.{radius}" for radius in ("0", "0.5", "1.5", "1.75", "2", "2.5", "2.85", "3")),
    "K07/integer_rounding", "K08/incremental_cancellation",
    *(f"K09/{dtype}.{shape}.{method}.{radius}" for dtype in DTYPES
      for shape in ("1x1", "1x7", "7x1") for method in METHODS for radius in range(3)),
    *(f"K10/{roi}.{method}" for roi in ("rectangle", "hole") for method in METHODS),
    *(f"K11/{roi}.{method}" for roi in ("absent", "point", "line") for method in METHODS),
    *(f"K14/{target}.{method}" for target in ("current", "all_stack") for method in METHODS),
    *(f"SZ01/{method}.radius2.signedzero" for method in METHODS[:3]),
)
REVISION_FIELDS = ("pixels", "metadata", "selection", "display")


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _keys(value, keys, name):
    _require(type(value) is dict and set(value) == set(keys), f"{name}: unexpected fields")


def _integer(value, lower, upper, name):
    _require(type(value) is int and lower <= value <= upper, f"{name}: expected bounded integer")


def _sha(value, name):
    _require(type(value) is str and re.fullmatch(r"[0-9a-f]{64}", value), f"{name}: invalid SHA256")


def _canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False,
                      allow_nan=False).encode("utf-8")


def _digest(value):
    return hashlib.sha256(_canonical(value)).hexdigest()


def _preflight(value):
    nodes = chars = 0

    def visit(item, depth):
        nonlocal nodes, chars
        nodes += 1
        _require(nodes <= 20000 and depth <= 10, "Input exceeds node/depth bound")
        if type(item) is dict:
            _require(len(item) <= 32, "Input object exceeds field bound")
            for key, child in item.items():
                _require(type(key) is str and len(key) <= 256, "Input key exceeds bound")
                chars += len(key)
                visit(child, depth + 1)
        elif type(item) is list:
            _require(len(item) <= 512, "Input array exceeds element bound")
            for child in item:
                visit(child, depth + 1)
        elif type(item) is str:
            chars += len(item)
            _require(len(item) <= 8192, "Input string exceeds bound")
        elif type(item) in (int, float):
            _require(abs(item) <= 2**53 and math.isfinite(item), "Input number exceeds finite bound")
        else:
            _require(item is None or type(item) is bool, "Input is not ordinary JSON")
        _require(chars <= 256 * 1024, "Input exceeds aggregate string bound")

    visit(value, 0)


def _roi(value, width, height):
    if value is None:
        return
    _require(type(value) is dict and type(value.get("kind")) is str, "Invalid ROI")
    fields = {"rectangle": ("xywh",), "holed_area": ("outer_xywh", "hole_xywh"),
              "point": ("xy",), "line": ("xyxy",)}
    kind = value["kind"]
    _require(kind in fields, "Unqualified ROI kind")
    _keys(value, ("kind", *fields[kind]), "ROI")
    for field in fields[kind]:
        coordinates = value[field]
        _require(type(coordinates) is list and len(coordinates) == (2 if field == "xy" else 4),
                 "Invalid ROI coordinate arity")
        for coordinate in coordinates:
            _integer(coordinate, 0, max(width, height), "ROI coordinate")
        if field.endswith("xywh"):
            x, y, w, h = coordinates
            _require(w > 0 and h > 0 and x + w <= width and y + h <= height, "ROI rectangle outside image")
        else:
            _require(all(x < width and y < height for x, y in zip(coordinates[::2], coordinates[1::2])),
                     "ROI point/line outside image")
    if kind == "holed_area":
        x, y, w, h = value["outer_xywh"]
        hx, hy, hw, hh = value["hole_xywh"]
        _require(hx >= x and hy >= y and hx + hw <= x + w and hy + hh <= y + h,
                 "ROI hole outside its outer rectangle")


def _validate_inputs(envelope):
    _preflight(envelope)
    _keys(envelope, ("schema_version", "kind", "fixture", "cases", "kernels"), "Input envelope")
    _integer(envelope["schema_version"], 1, 1, "Input schema")
    _require(envelope["kind"] == "rank_finite_scalar_observer_inputs_v1", "Unqualified input kind")
    pin = envelope["fixture"]
    _keys(pin, ("path", "bytes", "sha256"), "Fixture identity")
    _require(pin["path"] == "tests/fixtures/rank_imagej_154r_001.json", "Unexpected fixture identity")
    _integer(pin["bytes"], 311070, 311070, "Fixture byte count")
    _require(pin["sha256"] == "121d1688e9a26c20818fbebfe88cbc4d1704ad26d330bfff5bdcc4d8e11694e5",
             "Unexpected fixture fingerprint")
    cases = envelope["cases"]
    _require(type(cases) is list and len(cases) == 161, "Exactly 161 ordered input cases required")
    total_bytes = 0
    for ordinal, case in enumerate(cases):
        _keys(case, ("case_id", "input_sha256", "input"), "Input case")
        _require(case["case_id"] == f"rank-{ordinal:03d}", "Formal case membership/order changed")
        _sha(case["input_sha256"], "Input fingerprint")
        source = case["input"]
        _keys(source, ("id", "source_case_id", "source_observation_sha256", "dtype", "shape_tczyx",
                       "position_tcz", "roi", "dispatch", "all_stack", "method", "radius", "planes"), "Typed input")
        _require(source["id"] == RUN_IDS[ordinal], "Captured input membership/order changed")
        _require(source["source_case_id"] == source["id"].split("/", 1)[0], "Captured source case changed")
        _sha(source["source_observation_sha256"], "Captured observation identity")
        _require(type(source["dtype"]) is str and source["dtype"] in DTYPES, "Unqualified scalar dtype")
        shape = source["shape_tczyx"]
        _require(type(shape) is list and len(shape) == 5, "Explicit TCZYX shape required")
        for dimension, maximum in zip(shape, (2, 3, 4, 25, 25)):
            _integer(dimension, 1, maximum, "TCZYX dimension")
        t_count, c_count, z_count, height, width = shape
        position = source["position_tcz"]
        _require(type(position) is list and len(position) == 3, "Explicit captured TCZ required")
        for coordinate, count in zip(position, shape[:3]):
            _integer(coordinate, 0, count - 1, "Captured TCZ")
        _require(type(source["dispatch"]) is str and source["dispatch"] in ("api", "runner"), "Invalid dispatch")
        _require(type(source["all_stack"]) is bool, "Stack request requires strict Boolean")
        _require(not source["all_stack"] or source["dispatch"] == "runner", "Stack input requires captured runner route")
        _require(type(source["method"]) is str and source["method"] in METHODS, "Unqualified rank method")
        radius = source["radius"]
        _require(type(radius) in (int, float) and math.isfinite(radius)
                 and 0 <= radius <= (100 if source["method"] == "median" else 1000), "Invalid finite rank radius")
        _roi(source["roi"], width, height)
        planes = source["planes"]
        _require(type(planes) is list and len(planes) == t_count * c_count * z_count <= 24,
                 "All bounded source planes must be retained")
        item_size = {"uint8": 1, "uint16": 2, "float32": 4}[source["dtype"]]
        for plane_ordinal, plane in enumerate(planes):
            _keys(plane, ("tcz", "before_hex"), "Source plane")
            coordinate = plane["tcz"]
            _require(type(coordinate) is list and len(coordinate) == 3, "Plane requires explicit TCZ")
            for value, count in zip(coordinate, shape[:3]):
                _integer(value, 0, count - 1, "Source plane coordinate")
            _require(coordinate == [plane_ordinal // (z_count * c_count), plane_ordinal % c_count,
                                    plane_ordinal // c_count % z_count], "Captured T-Z-C plane order changed")
            bits = plane["before_hex"]
            _require(type(bits) is str and len(bits) == 2 * height * width * item_size
                     and len(bits) <= 5000 and re.fullmatch(r"[0-9a-f]+", bits), "Invalid declared source bytes")
            total_bytes += len(bits) // 2
            _require(total_bytes <= 128 * 1024, "Source pixel-byte budget exceeded")
    kernels = envelope["kernels"]
    _require(type(kernels) is list and len(kernels) == 10, "Exactly ten auxiliary kernel requests required")
    for kernel, radius in zip(kernels, RADII):
        _keys(kernel, ("id", "radius"), "Auxiliary kernel request")
        _require(kernel["id"] == "K01/" + str(radius), "Auxiliary kernel identity/order changed")
        _require(type(kernel["radius"]) in (int, float) and kernel["radius"] == radius,
                 "Auxiliary kernel radius changed")
    # All declared allocation bounds above precede any byte decode or array creation.
    for case in cases:
        source = case["input"]
        _require(_digest(source) == case["input_sha256"], "Complete typed input fingerprint differs")
        if source["dtype"] == "float32":
            for plane in source["planes"]:
                _require(all(bits & 0x7f800000 != 0x7f800000 for (bits,) in
                             struct.iter_unpack("<I", bytes.fromhex(plane["before_hex"]))),
                         "Nonfinite Float32 source samples are unqualified")


def _strict_json(raw):
    def pairs(values):
        result = {}
        for key, value in values:
            _require(key not in result, "Duplicate JSON object key")
            result[key] = value
        return result

    def constant(value):
        raise ValueError(f"Nonfinite JSON constant: {value}")

    return json.loads(raw, object_pairs_hook=pairs, parse_constant=constant)


def _file_pin(path):
    path = Path(path).resolve(strict=True)
    _require(path.is_file(), "Runtime origin is not a regular file")
    size = path.stat().st_size
    _require(0 < size <= 64 * 1024 * 1024, "Runtime origin size is outside its bound")
    digest = hashlib.sha256()
    count = 0
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            count += len(chunk)
            _require(count <= 64 * 1024 * 1024, "Runtime origin grew beyond its byte bound")
            digest.update(chunk)
    _require(count == size, "Runtime origin size changed during hashing")
    return {"path": str(path), "bytes": count, "sha256": digest.hexdigest()}


def _load_package(package_root):
    root = Path(package_root).resolve(strict=True)
    package = root if root.name == "imagejxx" else root / "imagejxx"
    package = package.resolve(strict=True)
    checkout = Path(__file__).resolve().parents[1] / "src" / "imagejxx"
    _require(package.is_dir() and package != checkout.resolve(), "An installed package root is required")
    _require(not any(name == "imagejxx" or name.startswith("imagejxx.") for name in sys.modules),
             "Refusing a preloaded product module")
    sys.path.insert(0, str(package.parent))
    ij = importlib.import_module("imagejxx")
    native = importlib.import_module("imagejxx._native")
    np = importlib.import_module("numpy")
    for module in (ij, native):
        origin = Path(module.__file__).resolve(strict=True)
        _require(origin.is_relative_to(package) and origin.parent == package, "Product import escaped exact package root")
    _require(not any(name == "imagejxx._desktop" or name.startswith("imagejxx.ui") for name in sys.modules),
             "Observer must remain core-only")
    return ij, native, np


def _array_bytes(np, array):
    return np.ascontiguousarray(array, dtype=array.dtype.newbyteorder("<")).tobytes(order="C")


def _array_sha(np, array):
    return hashlib.sha256(_array_bytes(np, array)).hexdigest()


def _selection(ij, np, source):
    roi = source["roi"]
    if roi is None:
        return None
    kind = roi["kind"]
    if kind in ("rectangle", "holed_area"):
        mask = np.zeros(source["shape_tczyx"][-2:], dtype=np.uint8)
        x, y, width, height = roi["xywh"] if kind == "rectangle" else roi["outer_xywh"]
        mask[y:y + height, x:x + width] = 1
        if kind == "holed_area":
            x, y, width, height = roi["hole_xywh"]
            mask[y:y + height, x:x + width] = 0
        return ij.Selection.from_mask(mask)
    if kind == "point":
        return ij.Selection("point", [tuple(roi["xy"])])
    coordinates = roi["xyxy"]
    return ij.Selection("line", [tuple(coordinates[:2]), tuple(coordinates[2:])])


def _position(snapshot):
    display = snapshot.display
    return [display.t, display.c, display.crosshair[2]]


def _case(ij, np, case):
    source = case["input"]
    dtype = {"uint8": "u1", "uint16": "<u2", "float32": "<f4"}[source["dtype"]]
    pixels = np.empty(source["shape_tczyx"], dtype=dtype)
    for plane in source["planes"]:
        pixels[tuple(plane["tcz"])] = np.frombuffer(bytes.fromhex(plane["before_hex"]), dtype=dtype).reshape(pixels.shape[-2:])
    document = ij.Document(ij.Dataset(pixels, axes="TCZYX"), "Rank fixture " + case["case_id"])
    view = document.snapshot().display
    view.t, view.c = source["position_tcz"][:2]
    crosshair = list(view.crosshair)
    crosshair[2] = source["position_tcz"][2]
    view.crosshair = crosshair
    document.set_display(view)
    document.select(_selection(ij, np, source))
    application = ij.Application()
    application.adopt(document)
    before = document.snapshot()
    before_array = before.dataset.numpy("TCZYX")
    source_sha = _array_sha(np, pixels)
    document_sha = _array_sha(np, before_array)
    history_before = (document.undo_steps, document.redo_steps)
    membership_before = list(application.document_ids)
    request = (ij.TargetRequest(ij.TargetScope.RANGES, t=ij.AxisRange(), c=ij.AxisRange(), z=ij.AxisRange())
               if source["all_stack"] else ij.TargetRequest())
    resolved = ij.resolve_rank_command(before, request, ij.RankFilterCommandOptions(source["method"], source["radius"]))
    target = resolved.target
    _require(0 < target.position_count <= 24, "Native target exceeds observation bound")
    positions = [[position.t, position.c, position.z]
                 for position in (target.position(index) for index in range(target.position_count))]
    captured = _position(target.source)
    prepared = application.prepare("rank_filter_imagej", target, resolved.parameters())
    preview = prepared.preview
    actual = preview.dataset.numpy("TCZYX")
    _require(actual.ndim == 5 and 0 < actual.nbytes <= 64 * 1024, "Native preview exceeds observation bound")
    shape = list(actual.shape)
    output_planes = [{"tcz": [t, c, z], "after_hex": _array_bytes(np, actual[t, c, z]).hex()}
                     for t in range(shape[0]) for z in range(shape[2]) for c in range(shape[1])]
    after = document.snapshot()
    after_array = after.dataset.numpy("TCZYX")
    invariants = {
        "source_sha256_before": source_sha, "source_sha256_after": _array_sha(np, pixels),
        "document_sha256_before": document_sha, "document_sha256_after": _array_sha(np, after_array),
        "source_shares_data_before_after": after.shares_data(before),
        "all_revisions_before": {name: getattr(before.revisions, name) for name in REVISION_FIELDS},
        "all_revisions_after": {name: getattr(after.revisions, name) for name in REVISION_FIELDS},
        "all_revisions_match": after.revisions.matches(before.revisions, ij.Change.ALL),
        "dirty_before": before.dirty, "dirty_after": after.dirty,
        "undo_steps_before": history_before[0], "undo_steps_after": document.undo_steps,
        "redo_steps_before": history_before[1], "redo_steps_after": document.redo_steps,
        "preview_shares_memory_with_source": bool(np.shares_memory(actual, pixels)),
        "target_positions_tcz": positions, "captured_position_tcz": captured,
        "document_id_before": before.id, "document_id_after": after.id,
        "application_document_ids_before": membership_before,
        "application_document_ids_after": list(application.document_ids),
    }
    row = {"case_id": case["case_id"], "input_sha256": case["input_sha256"],
           "observed": {"id": source["id"], "dtype": actual.dtype.name, "shape_tczyx": shape,
                        "position_tcz": _position(preview), "planes": output_planes},
           "invariants": invariants}
    # Plain JSON values only escape this function; all prepared/native owners die
    # before the next case. No edit is published and no async lease is claimed.
    return row


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--package-root", required=True)
    args = parser.parse_args(argv)
    input_path = Path(args.inputs).resolve(strict=True)
    _require(input_path.is_file() and input_path.stat().st_size <= MAX_INPUT_BYTES, "Input envelope exceeds bound")
    with input_path.open("rb") as stream:
        raw = stream.read(MAX_INPUT_BYTES + 1)
    _require(0 < len(raw) <= MAX_INPUT_BYTES, "Input envelope exceeds bound")
    envelope = _strict_json(raw)
    _validate_inputs(envelope)
    output_path = Path(args.output).absolute()
    _require(output_path.parent.is_dir() and not output_path.exists(), "A fresh output path with existing parent is required")
    ij, native, np = _load_package(args.package_root)
    runtime = {"python": {"executable": str(Path(sys.executable).resolve(strict=True)), "version": sys.version},
               "numpy": {"version": np.__version__}, "imagejxx": _file_pin(ij.__file__), "_native": _file_pin(native.__file__)}
    started = time.perf_counter()
    cases = []
    for case in envelope["cases"]:
        _require(time.perf_counter() - started <= 90, "Observer elapsed budget exceeded between native calls")
        cases.append(_case(ij, np, case))
    kernels = []
    for request in envelope["kernels"]:
        actual = ij.rank_filter_kernel_imagej(request["radius"])
        _require(set(actual) == {"half_widths", "point_count"}, "Unexpected native kernel metadata")
        widths = list(actual["half_widths"])
        _require(len(widths) <= 21 and all(type(width) is int and 0 <= width <= 10 for width in widths),
                 "Native kernel exceeds observation bound")
        middle = len(widths) // 2
        offsets = [[x, row - middle] for row, half in enumerate(widths) for x in range(-half, half + 1)]
        kernels.append({"id": request["id"], "radius": request["radius"], "half_widths": widths,
                        "point_count": actual["point_count"], "offsets_xy": offsets})
    report = {"schema_version": 1, "kind": "rank_finite_scalar_observations_v1",
              "input_manifest": {"path": str(input_path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()},
              "runtime": runtime, "cases": cases, "kernels": kernels}
    encoded = _canonical(report) + b"\n"
    _require(len(encoded) <= MAX_OUTPUT_BYTES, "Observation output exceeds byte bound")
    with output_path.open("xb") as stream:
        stream.write(encoded)
    print(json.dumps({"path": str(output_path), "bytes": len(encoded), "sha256": hashlib.sha256(encoded).hexdigest()}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
