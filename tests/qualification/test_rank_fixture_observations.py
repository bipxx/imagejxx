"""Independent read-only qualification of current installed rank observations.

No product, observer or parity adapter is imported or executed.  Captured ImageJ
bytes supply expectations; the two aggregate tests keep 161 filtered outputs
separate from ten auxiliary footprints.  Required evidence never becomes a skip.
"""

import hashlib
import json
import math
import os
from pathlib import Path
import re
import stat
import struct
import zipfile

import pytest


ROOT = Path(__file__).resolve().parents[2]
FIXTURE_PATH = ROOT / "tests/fixtures/rank_imagej_154r_001.json"
FIXTURE_PIN = {
    "path": "tests/fixtures/rank_imagej_154r_001.json", "bytes": 311070,
    "sha256": "121d1688e9a26c20818fbebfe88cbc4d1704ad26d330bfff5bdcc4d8e11694e5",
}
RUNTIME_PINS = {
    "imagejxx": (45731, "37ba3038b3ed15d125323c457ca4378876d022e53863bd578099dbaeafe328a6"),
    "_native": (3591680, "b24161cb5850f851c62a7c95aa9afd552c11a920382f3becef58f738d42d4527"),
}
INPUT_FIELDS = (
    "id", "source_case_id", "source_observation_sha256", "dtype", "shape_tczyx",
    "position_tcz", "roi", "dispatch", "all_stack", "method", "radius",
)
INVARIANT_FIELDS = {
    "source_sha256_before", "source_sha256_after", "document_sha256_before", "document_sha256_after",
    "source_shares_data_before_after", "all_revisions_before", "all_revisions_after", "all_revisions_match",
    "dirty_before", "dirty_after", "undo_steps_before", "undo_steps_after", "redo_steps_before", "redo_steps_after",
    "preview_shares_memory_with_source", "target_positions_tcz", "captured_position_tcz",
    "document_id_before", "document_id_after", "application_document_ids_before", "application_document_ids_after",
}
ALL_STACK_TARGET_TCZ = [
    [0, 0, 0], [0, 0, 1], [0, 0, 2], [0, 0, 3], [0, 1, 0], [0, 1, 1],
    [0, 1, 2], [0, 1, 3], [0, 2, 0], [0, 2, 1], [0, 2, 2], [0, 2, 3],
    [1, 0, 0], [1, 0, 1], [1, 0, 2], [1, 0, 3], [1, 1, 0], [1, 1, 1],
    [1, 1, 2], [1, 1, 3], [1, 2, 0], [1, 2, 1], [1, 2, 2], [1, 2, 3],
]
IMAGEJ_PLANE_TCZ = [
    [0, 0, 0], [0, 1, 0], [0, 2, 0], [0, 0, 1], [0, 1, 1], [0, 2, 1],
    [0, 0, 2], [0, 1, 2], [0, 2, 2], [0, 0, 3], [0, 1, 3], [0, 2, 3],
    [1, 0, 0], [1, 1, 0], [1, 2, 0], [1, 0, 1], [1, 1, 1], [1, 2, 1],
    [1, 0, 2], [1, 1, 2], [1, 2, 2], [1, 0, 3], [1, 1, 3], [1, 2, 3],
]
KERNEL_IDS = (
    "K01/0.0", "K01/0.5", "K01/1.0", "K01/1.5", "K01/1.75",
    "K01/2.0", "K01/2.5", "K01/2.85", "K01/3.0", "K01/10.0",
)
MAX_JSON_BYTES = 4 * 1024 * 1024
WHEEL_PIN = {
    "path": str(ROOT / "build/p06f-dist-ui003/imagejxx-0.2.0-cp312-cp312-win_amd64.whl"),
    "bytes": 14473581,
    "sha256": "fadb80559ccdee33e87f95b0d976976fa87725e064f0010daf88fa8ea6e7840f",
}
WHEEL_MEMBERS = {"imagejxx": "imagejxx/__init__.py", "_native": "imagejxx/_native.cp312-win_amd64.pyd"}


def _require(condition, message):
    if not condition:
        raise AssertionError(message)


def _keys(value, expected, where):
    _require(type(value) is dict and set(value) == set(expected), f"{where}: object fields differ")


def _integer(value, where, minimum=0):
    _require(type(value) is int and minimum <= value <= 2**64 - 1, f"{where}: invalid integer")


def _strict_same(actual, expected, where):
    _require(type(actual) is type(expected), f"{where}: JSON value type differs")
    if type(expected) is dict:
        _keys(actual, expected, where)
        for key in expected:
            _strict_same(actual[key], expected[key], f"{where}/{key}")
    elif type(expected) is list:
        _require(len(actual) == len(expected), f"{where}: array length differs")
        for index, (left, right) in enumerate(zip(actual, expected)):
            _strict_same(left, right, f"{where}/{index}")
    elif type(expected) is float:
        _require(math.isfinite(actual) and struct.pack("<d", actual) == struct.pack("<d", expected),
                 f"{where}: floating metadata bits differ")
    else:
        _require(actual == expected, f"{where}: value differs")


def _identity(info):
    timestamp = getattr(info, "st_birthtime_ns", info.st_ctime_ns) if os.name == "nt" else info.st_ctime_ns
    return info.st_dev, info.st_ino, info.st_mode, info.st_size, info.st_mtime_ns, timestamp


def _safe_stat(path):
    info = os.lstat(path)
    _require(not stat.S_ISLNK(info.st_mode)
             and not getattr(info, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400),
             f"Link/reparse evidence path refused: {path}")
    return info


def _read_regular(path, limit):
    for parent in reversed(path.parents):
        _require(stat.S_ISDIR(_safe_stat(parent).st_mode), f"Invalid evidence parent: {parent}")
    before = _safe_stat(path)
    _require(stat.S_ISREG(before.st_mode) and 0 < before.st_size <= limit, f"Evidence size/type refused: {path}")
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags)
    with os.fdopen(descriptor, "rb") as stream:
        _require(_identity(os.fstat(stream.fileno())) == _identity(before), f"Evidence changed on open: {path}")
        payload = stream.read(limit + 1)
        _require(_identity(os.fstat(stream.fileno())) == _identity(before), f"Evidence changed on read: {path}")
    _require(len(payload) == before.st_size and _identity(_safe_stat(path)) == _identity(before),
             f"Evidence changed while reading: {path}")
    return payload


def _preflight_json(payload, where):
    """Bound tokens, nesting and containers before invoking the JSON decoder."""
    _require(0 < len(payload) <= MAX_JSON_BYTES, f"{where}: JSON byte bound exceeded")
    text = payload.decode("utf-8", errors="strict")
    _require(not text.startswith("\ufeff"), f"{where}: UTF-8 BOM is not allowed")
    containers = []
    inside = escaped = False
    length = total_chars = nodes = token_length = 0
    for character in text:
        if inside:
            length += 1
            total_chars += 1
            _require(length <= 8192 and total_chars <= 2 * 1024 * 1024, f"{where}: string bound exceeded")
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                inside = False
            continue
        if character == '"':
            inside, length, token_length = True, 0, 0
            nodes += 1
        elif character in "[{":
            containers.append([character, 0])
            _require(len(containers) <= 16, f"{where}: nesting bound exceeded")
            token_length = 0
            nodes += 1
        elif character in "]}":
            _require(containers and containers[-1][0] == (
                "[" if character == "]" else "{"), f"{where}: mismatched JSON container")
            containers.pop()
            token_length = 0
        elif character == ",":
            _require(bool(containers), f"{where}: comma outside a container")
            containers[-1][1] += 1
            limit = 512 if containers[-1][0] == "[" else 64
            _require(containers[-1][1] < limit, f"{where}: container member bound exceeded")
            token_length = 0
        elif character in " \t\r\n:":
            token_length = 0
        else:
            nodes += token_length == 0
            token_length += 1
            _require(token_length <= 64, f"{where}: scalar token bound exceeded")
        _require(nodes <= 50000, f"{where}: JSON node bound exceeded")
    _require(not inside and not containers, f"{where}: incomplete JSON structure")
    return text


def _json(payload, where):
    text = _preflight_json(payload, where)

    def pairs(entries):
        result = {}
        for key, value in entries:
            _require(key not in result, f"{where}: duplicate JSON key {key!r}")
            result[key] = value
        return result

    def reject_constant(value):
        raise AssertionError(f"{where}: nonfinite JSON constant {value}")

    def finite_float(value):
        result = float(value)
        _require(math.isfinite(result), f"{where}: nonfinite JSON number")
        return result

    return json.loads(text, object_pairs_hook=pairs, parse_constant=reject_constant, parse_float=finite_float)


def _digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                    ensure_ascii=True, allow_nan=False).encode("utf-8")).hexdigest()


def _env_path(name):
    value = os.environ.get(name)
    _require(type(value) is str and bool(value), f"Required evidence environment variable is missing: {name}")
    path = Path(value)
    _require(path.is_absolute() and str(path) == os.path.abspath(path), f"{name}: use a canonical absolute path")
    return path


def _path_same(left, right):
    return os.path.normcase(os.path.abspath(left)) == os.path.normcase(os.path.abspath(right))


def _expected_input(row):
    return {**{field: row[field] for field in INPUT_FIELDS},
            "planes": [{"tcz": plane["tcz"], "before_hex": plane["before_hex"]} for plane in row["planes"]]}


def _source_sha256(row):
    # Input planes arrive in ImageJ T-Z-C order.  ndarray bytes are TCZYX, so
    # concatenate the same literal bytes in T-C-Z order, without scalar casts.
    ordered = sorted(row["planes"], key=lambda plane: tuple(plane["tcz"]))
    return hashlib.sha256(b"".join(bytes.fromhex(plane["before_hex"]) for plane in ordered)).hexdigest()


def _runtime(runtime, package_root, pins):
    _keys(runtime, ("python", "numpy", "imagejxx", "_native"), "runtime")
    _keys(runtime["python"], ("executable", "version"), "runtime/python")
    _keys(runtime["numpy"], ("version",), "runtime/numpy")
    _require(type(runtime["python"]["version"]) is str and runtime["python"]["version"], "Missing actual Python version")
    executable = runtime["python"]["executable"]
    _require(type(executable) is str and Path(executable).is_absolute(), "Missing absolute actual Python executable")
    _require(runtime["numpy"]["version"] == "2.2.6", "Unexpected qualified NumPy version")
    _require(stat.S_ISDIR(_safe_stat(package_root).st_mode), "Expected package root is not a directory")
    for role, (expected_size, expected_sha) in RUNTIME_PINS.items():
        pin = runtime[role]
        _keys(pin, ("path", "bytes", "sha256"), f"runtime/{role}")
        _integer(pin["bytes"], f"runtime/{role}/bytes", 1)
        _require(pin["bytes"] == expected_size and pin["sha256"] == expected_sha, f"runtime/{role}: unqualified file pin")
        _require(type(pin["path"]) is str and Path(pin["path"]).is_absolute(), f"runtime/{role}: nonabsolute origin")
        path = Path(pin["path"])
        _require(_path_same(path.parent, package_root), f"runtime/{role}: origin is outside installed package root")
        if role == "imagejxx":
            _require(path.name == "__init__.py", "Package origin must be installed __init__.py")
        else:
            _require(path.name.startswith("_native.") and path.suffix == ".pyd", "Native origin must be the installed extension")
        payload = _read_regular(path, expected_size)
        _require(len(payload) == expected_size and hashlib.sha256(payload).hexdigest() == expected_sha,
                 f"runtime/{role}: installed bytes differ")
        pins.append((path, expected_size, expected_sha))


@pytest.fixture(scope="module")
def observations():
    observed_path = _env_path("IMAGEJXX_RANK_FIXTURE_OBSERVATIONS")
    input_path = _env_path("IMAGEJXX_RANK_FIXTURE_INPUTS")
    package_root = _env_path("IMAGEJXX_RANK_FIXTURE_PACKAGE_ROOT")
    provenance_path = _env_path("IMAGEJXX_RANK_FIXTURE_PROVENANCE")
    fixture_bytes = _read_regular(FIXTURE_PATH, FIXTURE_PIN["bytes"])
    _require(len(fixture_bytes) == FIXTURE_PIN["bytes"]
             and hashlib.sha256(fixture_bytes).hexdigest() == FIXTURE_PIN["sha256"], "Portable fixture changed")
    fixture = _json(fixture_bytes, "portable fixture")
    input_bytes = _read_regular(input_path, MAX_JSON_BYTES)
    observed_bytes = _read_regular(observed_path, MAX_JSON_BYTES)
    provenance_bytes = _read_regular(provenance_path, MAX_JSON_BYTES)
    inputs = _json(input_bytes, "observer inputs")
    actual = _json(observed_bytes, "observer observations")
    provenance = _json(provenance_bytes, "runtime provenance")
    _keys(inputs, ("schema_version", "kind", "fixture", "cases", "kernels"), "observer inputs")
    _strict_same(inputs["schema_version"], 1, "input/schema_version")
    _require(inputs["kind"] == "rank_finite_scalar_observer_inputs_v1", "Wrong input kind")
    _strict_same(inputs["fixture"], FIXTURE_PIN, "input/fixture")
    _require(fixture["run_count"] == len(fixture["runs"]) == 161
             and fixture["kernel_count"] == len(fixture["kernels"]) == 10, "Fixture denominator changed")
    expected_inputs = [{"case_id": f"rank-{index:03d}", "input_sha256": _digest(_expected_input(row)),
                        "input": _expected_input(row)} for index, row in enumerate(fixture["runs"])]
    _strict_same(inputs["cases"], expected_inputs, "complete input cases")
    _strict_same(inputs["kernels"], [{"id": row["id"], "radius": row["radius"]} for row in fixture["kernels"]],
                 "complete auxiliary inputs")
    _keys(actual, ("schema_version", "kind", "input_manifest", "runtime", "cases", "kernels"), "observer observations")
    _strict_same(actual["schema_version"], 1, "observation/schema_version")
    _require(actual["kind"] == "rank_finite_scalar_observations_v1", "Wrong observation kind")
    manifest = actual["input_manifest"]
    _keys(manifest, ("path", "bytes", "sha256"), "observed input_manifest")
    _require(type(manifest["path"]) is str and Path(manifest["path"]).is_absolute()
             and _path_same(manifest["path"], input_path), "Observed input manifest path differs")
    _strict_same(manifest["bytes"], len(input_bytes), "observed input_manifest/bytes")
    _require(manifest["sha256"] == hashlib.sha256(input_bytes).hexdigest(), "Observed input manifest digest differs")
    _require(type(actual["cases"]) is list and len(actual["cases"]) == 161, "Incomplete current finite observations")
    _require(type(actual["kernels"]) is list and len(actual["kernels"]) == 10, "Incomplete current footprint observations")
    pins = [(FIXTURE_PATH, len(fixture_bytes), hashlib.sha256(fixture_bytes).hexdigest()),
            (input_path, len(input_bytes), hashlib.sha256(input_bytes).hexdigest()),
            (observed_path, len(observed_bytes), hashlib.sha256(observed_bytes).hexdigest()),
            (provenance_path, len(provenance_bytes), hashlib.sha256(provenance_bytes).hexdigest())]
    _runtime(actual["runtime"], package_root, pins)
    return fixture, inputs, actual, provenance, pins


def _unchanged(pins):
    for path, size, expected_sha in pins:
        payload = _read_regular(path, size)
        _require(len(payload) == size and hashlib.sha256(payload).hexdigest() == expected_sha,
                 f"Qualified evidence/source bytes changed: {path}")


def _pinned_source(pin, where):
    _keys(pin, ("path", "bytes", "sha256"), where)
    _require(type(pin["path"]) is str and Path(pin["path"]).is_absolute(), f"{where}: nonabsolute path")
    _integer(pin["bytes"], f"{where}/bytes", 1)
    _require(pin["bytes"] <= MAX_JSON_BYTES, f"{where}: source byte bound exceeded")
    _require(type(pin["sha256"]) is str and re.fullmatch(r"[0-9a-f]{64}", pin["sha256"]) is not None,
             f"{where}: invalid source digest")
    path = Path(pin["path"])
    payload = _read_regular(path, pin["bytes"])
    _require(len(payload) == pin["bytes"] and hashlib.sha256(payload).hexdigest() == pin["sha256"],
             f"{where}: declared source bytes differ")
    return path, payload


def _provenance(provenance, runtime, pins):
    _keys(provenance, ("schema_version", "kind", "wheel", "members", "source_snapshots"), "runtime provenance")
    _strict_same(provenance["schema_version"], 1, "provenance/schema_version")
    _require(provenance["kind"] == "rank_fixture_runtime_provenance_v1", "Wrong runtime provenance kind")
    wheel = provenance["wheel"]
    _keys(wheel, ("path", "bytes", "sha256"), "provenance/wheel")
    _require(type(wheel["path"]) is str and Path(wheel["path"]).is_absolute()
             and _path_same(wheel["path"], WHEEL_PIN["path"]), "Wrong immutable qualified wheel path")
    _strict_same(wheel["bytes"], WHEEL_PIN["bytes"], "provenance/wheel/bytes")
    _require(wheel["sha256"] == WHEEL_PIN["sha256"], "Wrong immutable qualified wheel digest")
    wheel_path = Path(wheel["path"])
    payload = _read_regular(wheel_path, wheel["bytes"])
    _require(len(payload) == wheel["bytes"] and hashlib.sha256(payload).hexdigest() == wheel["sha256"],
             "Qualified wheel bytes changed")
    del payload
    pins.append((wheel_path, wheel["bytes"], wheel["sha256"]))
    members = provenance["members"]
    _require(type(members) is list and len(members) == 2, "Both runtime wheel members are required")
    seen = set()
    with zipfile.ZipFile(wheel_path, "r") as archive:
        for member in members:
            _keys(member, ("runtime_role", "member", "bytes", "sha256"), "provenance/wheel member")
            role = member["runtime_role"]
            _require(type(role) is str and role in WHEEL_MEMBERS and role not in seen, "Duplicate/unknown runtime member role")
            seen.add(role)
            _require(member["member"] == WHEEL_MEMBERS[role], f"Wrong qualified wheel member for {role}")
            _strict_same(member["bytes"], runtime[role]["bytes"], f"wheel/runtime {role} byte count")
            _require(member["sha256"] == runtime[role]["sha256"], f"wheel/runtime {role} digest differs")
            _require(Path(runtime[role]["path"]).name == member["member"].split("/")[-1],
                     f"wheel/runtime {role} file name differs")
            info = archive.getinfo(member["member"])
            _require(not info.is_dir() and info.file_size == member["bytes"]
                     and 0 <= info.compress_size <= wheel["bytes"], f"Wheel member size/type differs: {role}")
            with archive.open(info, "r") as stream:
                member_bytes = stream.read(member["bytes"] + 1)
            _require(len(member_bytes) == member["bytes"]
                     and hashlib.sha256(member_bytes).hexdigest() == member["sha256"],
                     f"Actual wheel member differs from installed runtime: {role}")
    _require(seen == set(WHEEL_MEMBERS), "Missing installed-to-wheel runtime binding")
    snapshots = provenance["source_snapshots"]
    _require(type(snapshots) is list and 3 <= len(snapshots) <= 32, "Missing or excessive executed source snapshots")
    required = {os.path.normcase(str(ROOT / name)) for name in (
        "scripts/observe_rank_fixture.py", "tests/qualification/test_rank_fixture_observations.py",
        "scripts/parity_fixtures.py",
    )}
    active_paths, snapshot_paths = set(), set()
    total_bytes = 0
    for pair in snapshots:
        _keys(pair, ("active", "snapshot"), "source snapshot pair")
        _keys(pair["active"], ("path", "bytes", "sha256"), "active executed source")
        _require(type(pair["active"]["path"]) is str
                 and Path(pair["active"]["path"]).is_absolute()
                 and Path(pair["active"]["path"]).is_relative_to(ROOT),
                 "Active executed source is outside the checkout")
        active, active_bytes = _pinned_source(pair["active"], "active executed source")
        snapshot, snapshot_bytes = _pinned_source(pair["snapshot"], "immutable executed source snapshot")
        active_key, snapshot_key = os.path.normcase(str(active)), os.path.normcase(str(snapshot))
        _require(active_key not in active_paths and snapshot_key not in snapshot_paths
                 and active_key not in snapshot_paths and snapshot_key not in active_paths and active_key != snapshot_key,
                 "Source snapshot paths are duplicate or alias active source")
        active_info, snapshot_info = _safe_stat(active), _safe_stat(snapshot)
        _require((active_info.st_dev, active_info.st_ino) != (snapshot_info.st_dev, snapshot_info.st_ino),
                 "Source snapshot must be a distinct retained file")
        active_paths.add(active_key)
        snapshot_paths.add(snapshot_key)
        _require(pair["active"]["bytes"] == pair["snapshot"]["bytes"]
                 and pair["active"]["sha256"] == pair["snapshot"]["sha256"]
                 and active_bytes == snapshot_bytes, "Active and retained executed source bytes differ")
        total_bytes += len(active_bytes)
        _require(total_bytes <= 16 * 1024 * 1024, "Aggregate executed source snapshot bound exceeded")
        pins.extend(((active, pair["active"]["bytes"], pair["active"]["sha256"]),
                     (snapshot, pair["snapshot"]["bytes"], pair["snapshot"]["sha256"])))
    _require(required <= active_paths, "Observer, reader or input-adapter executed source snapshot is missing")


def _invariants(actual, source, where):
    _keys(actual, INVARIANT_FIELDS, where)
    expected_sha = _source_sha256(source)
    for name in ("source_sha256_before", "source_sha256_after", "document_sha256_before", "document_sha256_after"):
        _require(actual[name] == expected_sha, f"{where}/{name}: source bytes changed or order differs")
    _require(actual["source_shares_data_before_after"] is True, f"{where}: native source storage changed")
    _require(actual["all_revisions_match"] is True, f"{where}: native revision match failed")
    _keys(actual["all_revisions_before"], ("pixels", "metadata", "selection", "display"), f"{where}/revisions")
    for field, value in actual["all_revisions_before"].items():
        _integer(value, f"{where}/revisions/{field}")
    _strict_same(actual["all_revisions_after"], actual["all_revisions_before"], f"{where}/unchanged revisions")
    for before, after in (("dirty_before", "dirty_after"), ("undo_steps_before", "undo_steps_after"),
                          ("redo_steps_before", "redo_steps_after")):
        if before == "dirty_before":
            _require(type(actual[before]) is bool, f"{where}: dirty must be an actual boolean")
        else:
            _integer(actual[before], f"{where}/{before}")
        _strict_same(actual[after], actual[before], f"{where}/{after}")
    _require(actual["preview_shares_memory_with_source"] is False, f"{where}: exported result aliases source ndarray")
    expected_targets = ALL_STACK_TARGET_TCZ if source["all_stack"] else [source["position_tcz"]]
    _strict_same(actual["target_positions_tcz"], expected_targets, f"{where}/target positions")
    _strict_same(actual["captured_position_tcz"], source["position_tcz"], f"{where}/captured position")
    _integer(actual["document_id_before"], f"{where}/document id", 1)
    _strict_same(actual["document_id_after"], actual["document_id_before"], f"{where}/document identity")
    _strict_same(actual["application_document_ids_before"], [actual["document_id_before"]], f"{where}/before membership")
    _strict_same(actual["application_document_ids_after"], [actual["document_id_before"]], f"{where}/after membership")


def test_rank_fixture_current_finite_scalar_observations(observations):
    fixture, inputs, actual, provenance, pins = observations
    _provenance(provenance, actual["runtime"], pins)
    _require(len({case["case_id"] for case in actual["cases"]}) == 161, "Duplicate current finite case identity")
    for index, (source, declared, case) in enumerate(zip(fixture["runs"], inputs["cases"], actual["cases"])):
        where = f"rank-{index:03d}"
        _keys(case, ("case_id", "input_sha256", "observed", "invariants"), where)
        _require(case["case_id"] == where and case["input_sha256"] == declared["input_sha256"], f"{where}: input binding differs")
        expected = {field: source[field] for field in ("id", "dtype", "shape_tczyx", "position_tcz")}
        expected["planes"] = [{"tcz": plane["tcz"], "after_hex": plane["after_hex"]} for plane in source["planes"]]
        _strict_same(case["observed"], expected, f"{where}/observed")
        _invariants(case["invariants"], source, f"{where}/invariants")
        targets = ALL_STACK_TARGET_TCZ if source["all_stack"] else [source["position_tcz"]]
        item_size = {"uint8": 1, "uint16": 2, "float32": 4}[source["dtype"]]
        expected_plane_bytes = source["shape_tczyx"][3] * source["shape_tczyx"][4] * item_size
        for plane, output in zip(source["planes"], case["observed"]["planes"]):
            _require(re.fullmatch(r"[0-9a-f]+", output["after_hex"]) is not None, f"{where}: malformed declared bytes")
            captured = bytes.fromhex(plane["after_hex"])
            observed = bytes.fromhex(output["after_hex"])
            _require(len(observed) == expected_plane_bytes and observed == captured, f"{where}: declared LE bits differ")
            if plane["tcz"] not in targets:
                _require(observed == bytes.fromhex(plane["before_hex"]), f"{where}: excluded source plane changed")
        if source["source_case_id"] == "K14":
            _require([plane["tcz"] for plane in case["observed"]["planes"]] == IMAGEJ_PLANE_TCZ,
                     f"{where}: ImageJ plane order differs")
            _require(_source_sha256(source) == "d1e8f8304a797b4bbe1422b596b01510e243cd9a6f74c4b3d30c5678b1a18469",
                     f"{where}: full TCZYX source-byte ordering differs")
    _unchanged(pins)


def test_rank_fixture_current_auxiliary_footprints(observations):
    fixture, inputs, actual, unused_provenance, pins = observations
    _require(tuple(row["id"] for row in actual["kernels"]) == KERNEL_IDS, "Auxiliary footprint membership/order differs")
    _strict_same(actual["kernels"], fixture["kernels"], "ten separately observed footprints")
    for observed, declared in zip(actual["kernels"], inputs["kernels"]):
        _keys(observed, ("id", "radius", "half_widths", "point_count", "offsets_xy"), "observed footprint")
        _strict_same(observed["id"], declared["id"], "footprint input id")
        _strict_same(observed["radius"], declared["radius"], "footprint input radius")
        _integer(observed["point_count"], "footprint point count", 1)
        _require(observed["point_count"] == len(observed["offsets_xy"]), "Footprint point-count/offset mismatch")
    _require(len(actual["cases"]) == 161 and len(actual["kernels"]) == 10,
             "Footprints must remain separate auxiliary observations")
    _unchanged(pins)
