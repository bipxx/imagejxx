"""Independent startup observation primitives; standard library only.

Reads retained producer bytes and never imports the product, creates an editor,
drives input, or treats a producer's acceptance/result field as an oracle.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import re
import stat
import struct

MIB = 1024 * 1024
CONTRACT = "p06j.saved-workspace-startup.001"
CONTRACT_PIN = {"bytes": 23982, "sha256": "5a22fb7872a9e505173d620657f5a66f3aa97d4d03c29d8677938cd94a27a014"}
UI_CONTEXTS = ("imgui_cpp", "imgui_python")
UI_CASES = tuple(f"SU{i:02}" for i in range(1, 11))
LAUNCH_CONTEXTS = ("cpp", "python")
LAUNCH_CASES = tuple(f"C{i:02}" for i in range(1, 13))
MAX_RAW = 16 * MIB
MAX_PAIRED_RAW = 32 * MIB
PINS = {}


def integer(value, low=0, high=2**64 - 1):
    assert type(value) is int and low <= value <= high, (value, low, high)
    return value


def unique(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, "Duplicate captured JSON key: " + key
        result[key] = value
    return result


def decode(raw):
    def bad(token):
        raise AssertionError("Nonfinite JSON token: " + token)
    result = json.loads(raw, object_pairs_hook=unique, parse_constant=bad)
    pending, count = [(result, 0)], 0
    while pending:
        value, depth = pending.pop()
        count += 1
        assert depth <= 128 and count <= 2_000_000
        if type(value) is dict:
            pending.extend((item, depth + 1) for item in value.values())
        elif type(value) is list:
            pending.extend((item, depth + 1) for item in value)
        elif type(value) is float:
            assert math.isfinite(value)
    return result


def json_file(path, maximum=MAX_RAW):
    path = Path(path)
    assert path.is_file() and 0 < path.stat().st_size <= maximum
    with path.open("rb") as stream:
        raw = stream.read(maximum + 1)
    assert 0 < len(raw) <= maximum
    return decode(raw)


def absolute(value):
    assert type(value) is str and value and Path(value).is_absolute()
    path = Path(value)
    info = path.lstat()
    assert not path.is_symlink() and not getattr(info, "st_file_attributes", 0) & 0x400
    return path.resolve(strict=True)


def pin(value, allow_empty=False):
    assert type(value) is dict and {"path", "bytes", "sha256"} <= set(value)
    path = absolute(value["path"])
    size = integer(value["bytes"], 0 if allow_empty else 1, 256 * MIB)
    digest = value["sha256"]
    assert type(digest) is str and re.fullmatch(r"[0-9a-f]{64}", digest)
    before = path.stat()
    assert stat.S_ISREG(before.st_mode) and before.st_size == size
    identity = (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
    key = (str(path), size, digest)
    if key not in PINS:
        observed, count = hashlib.sha256(), 0
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(MIB), b""):
                count += len(block)
                assert count <= size
                observed.update(block)
        assert count == size and observed.hexdigest() == digest, "Evidence pin differs: " + str(path)
        PINS[key] = identity
    assert PINS[key] == identity
    return path


def recheck():
    for (name, size, _), identity in PINS.items():
        info = Path(name).stat()
        assert info.st_size == size and (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns) == identity


def inside(path, root):
    assert path == root or root in path.parents, (path, root)


def source_set(sources):
    rows = [{key: row[key] for key in ("role", "logical_path", "bytes", "sha256")}
            for row in sorted(sources, key=lambda item: item["role"])]
    return hashlib.sha256(json.dumps(rows, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def archive_bound(root):
    pending, entries, files, total = [root], 0, 0, 0
    while pending:
        with os.scandir(pending.pop()) as iterator:
            for entry in iterator:
                entries += 1
                info = entry.stat(follow_symlinks=False)
                assert entries <= 16384 and not entry.is_symlink()
                assert not getattr(info, "st_file_attributes", 0) & 0x400
                if stat.S_ISDIR(info.st_mode):
                    pending.append(Path(entry.path))
                else:
                    assert stat.S_ISREG(info.st_mode)
                    files += 1; total += info.st_size
                    assert files <= 8192 and total <= 64*MIB
    return total


def provenance(root, *, kind, contexts, cases, source_roles):
    root = absolute(str(root))
    archive_bound(root)
    value = json_file(root / "provenance.json", 2 * MIB)
    assert value["schema_version"] == 1 and value["kind"] == kind
    assert set(value["contexts"]) == set(contexts)
    roles = {}
    for source in value["sources"]:
        role = source["role"]
        assert type(role) is str and role not in roles
        inside(pin(source), root)
        logical = source["logical_path"].replace("\\", "/")
        if role in source_roles:
            assert logical == source_roles[role]
        roles[role] = source
    assert set(source_roles) <= set(roles) and len(roles) <= 128
    assert value["source_set_sha256"] == source_set(value["sources"])
    selected = [(case, context) for case in cases for context in contexts]
    assert [(row["id"], row["context"]) for row in value["cases"]] == selected
    assert sum(integer(row["raw"]["bytes"], 1, MAX_RAW) for row in value["cases"]) <= MAX_PAIRED_RAW
    for row in value["cases"]:
        inside(pin(row["raw"]), root)
    for context, binding in value["contexts"].items():
        installed = absolute(binding["installed_root"])
        for role in ("executable", "producer_artifact", "native_artifact"):
            product = pin(binding[role])
            if context in ("cpp", "imgui_cpp") or role != "executable":
                inside(product, installed)
        receipt = json_file(pin(binding["build_receipt"]), 2 * MIB)
        assert receipt["schema_version"] == 1
        assert receipt["kind"] == ("p06j.normal-launcher-source-product-binding.001" if "normal-launcher" in kind
                                    else "p06j.startup-workspace-source-product-binding.001")
        assert receipt["source_set_sha256"] == value["source_set_sha256"]
        assert receipt["sources"] == value["sources"] and receipt["acceptance_claim"] is False
        passed = json_file(pin(receipt["passed_build_receipt"]), 8 * MIB)
        assert passed["state"] == "passed"
        assert passed["source_freeze"] == receipt["source_freeze"]
        selected = json_file(pin(receipt["explicit_product_bindings"]), 2*MIB)
        assert selected["source_freeze"] == receipt["source_freeze"]
        assert selected["build_receipt"] == receipt["passed_build_receipt"]
        assert passed["built_products"] == selected["built_products"]
        for product in passed["built_products"].values(): pin(product)
        declared_context = "imgui_cpp" if context in ("cpp", "imgui_cpp") else "imgui_python"
        assert receipt["products"][context] == selected["products"][declared_context]
        for role in ("executable", "producer_artifact", "native_artifact"):
            assert binding[role] == selected["products"][declared_context][role]
        frozen = json_file(pin(receipt["source_freeze"]), 8*MIB)
        frozen_sources = {row["logical_path"]: row for row in frozen["sources"]}
        assert len(frozen_sources) == len(frozen["sources"])
        for source in value["sources"]:
            original = frozen_sources[source["logical_path"]]
            assert (source["bytes"], source["sha256"]) == (original["bytes"], original["sha256"])
    adopted = value["adopted_contract"]
    assert {key: adopted[key] for key in CONTRACT_PIN} == CONTRACT_PIN
    contract = json_file(pin(adopted), 128 * 1024)
    assert contract["id"] == CONTRACT
    return root, value, roles


def producer_envelope(raw, descriptor, binding, *, schema, case, context):
    assert raw["schema"] == schema and raw["version"] == 1
    assert raw["contract"] == CONTRACT and (raw["case_id"], raw["context"]) == (case, context)
    assert raw["observation_state"] == "observed" and raw["acceptance_claim"] is False
    assert not {"expected", "PASS", "pass", "passed", "outcome", "result"}.intersection(raw)
    assert absolute(raw["raw_report_path"]) == pin(descriptor["raw"])
    assert type(raw["elapsed_seconds"]) in (int, float) and 0 < raw["elapsed_seconds"] <= 15
    runtime = raw["runtime_before"]
    assert runtime == raw["runtime_after"] and runtime["pointer_bits"] == 64
    integer(runtime["process_id"], 1)
    assert type(runtime["imgui"]) is str and runtime["imgui"]
    for role in ("executable", "producer_artifact", "native_artifact"):
        assert runtime[role] == {key: binding[role][key] for key in ("path", "bytes", "sha256")}
        pin(runtime[role])
    if context == "imgui_python":
        origin = json_file(pin(descriptor["launcher_origin"]), 128 * 1024)
        assert origin["schema_version"] == 1 and origin["kind"] == "p06j.startup-workspace-python-launcher-origin.001"
        assert (origin["case_id"], origin["context"], origin["process_id"]) == (case, context, runtime["process_id"])
        assert origin["before"] == origin["after"]
        for role in ("executable", "producer_artifact", "native_artifact"):
            assert origin["before"][role] == binding[role]


def checkpoint(raw, phase):
    matches = [item for item in raw["checkpoints"] if item["phase"] == phase]
    assert len(matches) == 1, "Missing/duplicate actual checkpoint: " + phase
    value = matches[0]
    index = integer(value["frame_index"], 0, len(raw["frames"]) - 1)
    assert raw["frames"][index]["phase"] == phase
    return value


def state(raw, phase):
    return checkpoint(raw, phase)["state"]


def report(raw, phase):
    return raw["frames"][checkpoint(raw, phase)["frame_index"]]["report"]


def lookup(value, path):
    assert type(path) is list and path
    for key in path:
        assert type(key) in (str, int)
        value = value[key]
    return value


def clicked(raw, phase, suffix):
    end = checkpoint(raw, phase)["frame_index"]
    rows = [item for item in raw["control_observations"] if item["phase"] == phase
            and item["selector"].endswith(suffix) and max(item["input_frames"]) <= end]
    assert rows, "Missing actual ImGui pointer input: " + phase + "/" + suffix
    for row in rows:
        observed = integer(row["observed_frame"], 0, end)
        control = lookup(raw["frames"][observed]["report"], row["control_path"])
        integer(control["id"], 1, 2**32 - 1)
        assert control["enabled"] is True and control.get("visible", True) is True
        a, b, c, d = control["rect"]
        assert all(type(v) in (int, float) and math.isfinite(v) for v in (a, b, c, d))
        assert a < c and b < d
        down = up = False
        for index in row["input_frames"]:
            frame = raw["frames"][integer(index, observed, end)]
            assert frame["phase"] == phase
            for event in frame["physical_inputs"]:
                if event["type"] == "button" and event["name"] == "left":
                    x, y = frame["report"]["input_frame"]["mouse_position"]
                    assert a <= x < c and b <= y < d
                    if "clip_rect" in control:
                        x0, y0, x1, y1 = control["clip_rect"]
                        assert x0 <= x < x1 and y0 <= y < y1
                    down |= event["down"] is True
                    up |= event["down"] is False
        assert down and up
    return rows


def typed_text(raw, phase, field, text):
    rows = clicked(raw, phase, field)
    ids = {lookup(raw["frames"][row["observed_frame"]]["report"], row["control_path"])["id"] for row in rows}
    # Identical text may have been delivered to a different widget earlier in
    # the phase (for example the file picker's path input). Bind this delivery
    # to the recorded target click, including its physical button release.
    start = min(max(row["input_frames"]) for row in rows) + 1
    end = checkpoint(raw, phase)["frame_index"]
    observed = False
    for frame in raw["frames"][start:end + 1]:
        if frame["phase"] != phase:
            continue
        item = frame["report"]["preferences_panel"].get("fields", {}).get(field.split(".", 1)[0])
        if item and any(event["type"] == "text" and event["value"] == text for event in frame["physical_inputs"]):
            editor = item.get("editor", item)
            physical = frame["report"]["input_frame"]
            assert editor["id"] in ids and editor["active"] is True
            assert physical["focus"]["active_id"] == physical["input_text"]["id"] == editor["id"]
            assert editor["enabled"] is True and editor.get("visible", True) is True and item["draft"] == text
            observed = True
    assert observed, "Missing focused native InputText delivery: " + field


def file_bytes(raw, phase, role="preferences"):
    matches = [item for item in checkpoint(raw, phase)["files"] if item["role"] == role]
    assert len(matches) == 1
    item = matches[0]
    assert type(item["exists"]) is bool
    if not item["exists"]:
        assert item["byte_count"] == 0 and item["hex"] == ""
        return None
    contents = bytes.fromhex(item["hex"])
    assert len(contents) == integer(item["byte_count"], 1, MAX_RAW)
    assert hashlib.sha256(contents).hexdigest() == item["sha256"]
    if "json" in item:
        assert decode(contents) == item["json"]
    return contents


def documents(value):
    ids = value["document_ids"]
    assert type(ids) is list and len(ids) == len(set(ids)) <= 4
    owners = value["documents"]
    rows = list(owners.values()) if type(owners) is dict else owners
    assert sorted(ids) == sorted(row["id"] for row in rows)
    return [next(row for row in rows if row["id"] == identity) for identity in ids]


def empty(value):
    assert value["document_ids"] == [] and value["active_document_id"] is None
    assert value["results"] == [] and value["workflow"] == []


def fresh(document):
    assert document["dirty"] is False
    assert document["revisions"] == {"pixels": 0, "metadata": 0, "selection": 0, "display": 0}
    assert document["history"]["undo_steps"] == document["history"]["redo_steps"] == 0


def literal_document(document, scientific, *, changed=False):
    shape = [2, 3, 2, 6, 8, 1] if scientific else [1, 1, 2, 6, 8, 1]
    dtype = "uint16" if scientific else "uint8"
    assert document["shape"] == shape and document["dtype"] == dtype
    values = [40*t + 30*c + 5*z + 2*y + x if scientific else 7*z + 3*y + x
              for t in range(shape[0]) for c in range(shape[1]) for z in range(2)
              for y in range(6) for x in range(8)]
    if changed and scientific:
        values[((1*3 + 2)*2 + 1)*48 + 2*8 + 3] = 113
    pixels = document["pixels"]
    assert pixels["shape"] == shape and pixels["order"] == "TCZYXS" and pixels["pixel_type"] == dtype
    expected = struct.pack("<" + ("H" if scientific else "B")*len(values), *values)
    assert bytes.fromhex(pixels["hex"]) == expected
    storage = bytes.fromhex(pixels["storage_f64le_hex"])
    assert len(storage) == len(values)*8
    assert [row[0] for row in struct.iter_unpack("<d", storage)] == values
    assert document["calibration"]["spacing"] == [1.25, .5, 2.5]
    display = document["display"]
    assert display["t"] == (1 if scientific else 0) and display["c"] == (2 if scientific else 0)
    assert display["crosshair"] == [3, 2, 1] and display["zoom"] == 2.25
    assert display["plane_zoom"] == [1.5, 2.5, 3.5]
    assert display["pan"] == [[.25, -.5], [1.25, -1.5], [2.25, -2.5]]
    assert display["orthogonal"] is scientific and display["linked_zoom"] is False
    assert document["selection"]["kind"] == "rectangle"
    assert document["selection"]["points"] == [[1, 1], [5, 4]]
    assert document["previous_selection"]["kind"] == "line"
    assert len(document["rois"]) == 1 and document["rois"][0]["z"] == 1
    assert document["metadata"]["extensions"]["vendor"] == {"opaque": [True, None, "retained"]}
    fresh(document)
    return values


def workspace(value, *, active=0, changed=False):
    rows = documents(value)
    assert len(rows) == 2
    literal_document(rows[0], False)
    literal_document(rows[1], True, changed=changed)
    assert value["active_document_id"] == value["document_ids"][active]
    assert value["results"] == [{"Value": 17}]
    assert value["workflow"] == [{"operation": "must_never_execute", "historical_id": 777}]
    assert value["settings"]["shortcut_bindings"] == ["analysis.measure=Ctrl+K"]
    return rows
