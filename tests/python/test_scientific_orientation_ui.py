"""Independent captured P06i shared-ImGui assertions; no product imports/launch.

All24 O01-O12/context cases remain required. Development may select the
explicit first-six subset, whose absent remaining captures still fail if run.
Owned sample bits, affine/time geometry and V2 records below never come from a
producer expected value, product extractor or success/PASS label.
"""
from functools import lru_cache
import ast
import base64
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import xml.etree.ElementTree as ET
import zlib

import pytest
import actual_observation_transport as _transport


CONTRACT = "p06i.scientific-orientation-ui.001"
CASES = tuple(f"O{index:02d}" for index in range(1, 13))
CONTEXTS = ("imgui_cpp", "imgui_python")
IMPLEMENTED_CASES = CASES
PENDING_CASES = ()
PARAMETERS = tuple((case, context) for case in CASES for context in CONTEXTS)
PARAMETER_IDS = tuple(case + "-" + context for case, context in PARAMETERS)
EXPECTED_COLLECTION_IDS = tuple("test_installed_scientific_orientation[" + value + "]" for value in PARAMETER_IDS)
EXPECTED_COLLECTION_COUNT = 24
MAX_REPORT = 16 * 1024 * 1024
MAX_ARCHIVE = 32 * 1024 * 1024
MAX_FRAMES = 180
MAX_SECONDS = 15
CAP = 512 * 1024 * 1024
SHAPE = [2, 3, 5, 7, 9, 1]
AXES = {"XY": (0, 1, 2), "XZ": (0, 2, 1), "YZ": (1, 2, 0)}
CALIBRATION = {
    "spacing": [.4, 1.2, 3.], "origin": [11., -7., 19.],
    "direction": [1., .25, -.5, .5, -1., .125, 0., .75, 1.5],
    "unit": "um", "frame_interval": .75, "time_origin": 2.25, "time_unit": "min",
}
CONTRACT_BYTES = 30672
CONTRACT_SHA256 = "930f20fa1215eafaff0591c0ad419b87c28ccc085fde4ba0a4f9086cda963a51"
ZERO_USAGE = dict(running=0, queued=0, running_bytes=0, retained_bytes=0, reserved_bytes=0)
PHYSICAL_KEYS = {"Ctrl", "Shift", "Alt", "Super", "A", "O", "W", "Enter", "Escape", "Tab",
                 "Left", "Right", "Up", "Down", "Backspace", "Delete", "Space",
                 "Home", "End", "PageUp", "PageDown"}
SOURCE_ROLES = {
    "producer": "src/desktop/scientific_orientation_ui_probe.cpp",
    "producer_header": "src/desktop/scientific_orientation_ui_probe.hpp",
    "window": "src/desktop/window.cpp", "window_header": "src/desktop/window.hpp",
    "headless_header": "src/desktop/headless_session_internal.hpp",
    "native_orientation": "src/native/axis_orientation_command.cpp",
    "native_orientation_header": "include/imagejxx/axis_orientation_command.hpp",
    "native_execution": "src/native/execution.cpp", "native_execution_header": "include/imagejxx/execution.hpp",
    "record_core": "src/native/axis_command_record.cpp", "record_core_header": "src/native/axis_command_record.hpp",
    "desktop_binding": "src/desktop/python.cpp", "python_wrapper": "src/imagejxx/ui/app.py",
    "observation_transport": "tests/python/actual_observation_transport.py",
    "observation_transport_header": "src/desktop/actual_observation_transport.hpp",
    "reader": "tests/python/test_scientific_orientation_ui.py",
}
# Independent positive choices: (plane, native first/last/step, R/F/T flags).
EXPECTED_OUTPUTS = {
    "O01": (),
    "O02": (("xy.000", "XY", 1, 4, 2, False, False, False),
            ("xy.100", "XY", 1, 4, 2, True, False, False)),
    "O03": (("xy.010", "XY", 1, 4, 2, False, True, False),
            ("xy.110", "XY", 1, 4, 2, True, True, False)),
    "O04": (("xz.001", "XZ", 1, 6, 2, False, False, True),
            ("xz.101", "XZ", 1, 6, 2, True, False, True)),
    "O05": (("yz.011", "YZ", 1, 8, 2, False, True, True),
            ("yz.111", "YZ", 1, 8, 2, True, True, True)),
    "O06": (("corrected.xz.111", "XZ", 1, 6, 2, True, True, True),),
}
_PINS = {}

# Additional context-specific fixture profiles for the remaining adopted cases.
# These are owned input recipes, not values selected from a product result.
SOURCE_PROFILES = {
    "scientific_u16": (SHAPE, "uint16"),
    "eager_u16": ([1, 1, 5, 7, 9, 1], "uint16"),
    "scientific_rgb": ([2, 3, 5, 7, 9, 3], "uint8"),
}


def _integer(value, low=0, high=2**64 - 1):
    assert type(value) is int and low <= value <= high
    return value


def _number(value):
    assert type(value) in (int, float) and math.isfinite(value)
    return value


def _boolean(value):
    assert type(value) is bool
    return value


def _unique(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, "Duplicate captured key: " + key
        result[key] = value
    return result


def _nonfinite(value):
    raise AssertionError("Nonfinite captured JSON token: " + value)


def _json(path, limit=MAX_REPORT):
    assert path.is_file() and not path.is_symlink(), "Missing required capture: " + str(path)
    assert 0 < path.stat().st_size <= limit
    raw = path.read_bytes()
    assert 0 < len(raw) <= limit
    row = json.loads(raw, object_pairs_hook=_unique, parse_constant=_nonfinite)
    pending, nodes = [(row, 0)], 0
    while pending:
        value, depth = pending.pop()
        nodes += 1
        assert depth <= 128 and nodes <= 2_000_000
        if type(value) is float:
            assert math.isfinite(value)
        elif type(value) is dict:
            pending.extend((item, depth + 1) for item in value.values())
        elif type(value) is list:
            pending.extend((item, depth + 1) for item in value)
    return row


def _path(value):
    assert type(value) is str and value
    normalized = value.replace("\\", "/").lower()
    protected = ("/installed-products001/temp001", "/installed-products001/cleanup001.json",
                 "/sdk-integration002/consumer001", "/sdk-integration003/consumer001",
                 "/sdk-integration003/cleanup-owned-consumer001.json",
                 "/installed-products003/temp001", "/cleanup-ui-products003-001.json")
    assert not any(part in normalized for part in protected), "Protected evidence path refused before resolution"
    path = Path(value)
    assert path.is_absolute() and not path.is_symlink()
    return path.resolve(strict=True)


def _pin(value, maximum=256 * 1024 * 1024):
    assert type(value) is dict and {"path", "bytes", "sha256"} <= set(value)
    size = _integer(value["bytes"], 1, maximum)
    digest = value["sha256"]
    assert type(digest) is str and re.fullmatch("[0-9a-f]{64}", digest)
    path = _path(value["path"])
    stat = path.stat()
    identity = (stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns)
    assert path.is_file() and stat.st_size == size
    key = (str(path), size, digest)
    if key not in _PINS:
        observed, count = hashlib.sha256(), 0
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                observed.update(block)
                count += len(block)
                assert count <= size
        assert count == size and observed.hexdigest() == digest
        _PINS[key] = identity
    assert _PINS[key] == identity, "Pinned evidence changed: " + str(path)
    return path


def _recheck():
    for (name, size, _), identity in _PINS.items():
        stat = Path(name).stat()
        assert stat.st_size == size
        assert (stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns) == identity


def _source_set(sources):
    values = [{key: item[key] for key in ("role", "logical_path", "bytes", "sha256")}
              for item in sorted(sources, key=lambda item: item["role"])]
    raw = json.dumps(values, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()


def _reader_source_repair(actual, captured, provenance):
    """Admit only the explicit reviewed base64-reader repair, never relabel a capture."""
    configured=os.environ.get("IMAGEJXX_SCIENTIFIC_ORIENTATION_UI_READER_REPAIR")
    assert configured, "Changed reader requires an explicit qualified repair manifest"
    manifest=_json(_path(configured),128*1024)
    assert manifest["schema_version"] == 1 and manifest["kind"] == "p06i.scientific-orientation-ui-reader-repair.001"
    assert manifest["repair_id"] == "O09.native_v3_base64_f64le.001"
    assert manifest["allowed_case_ids"] == ["O09"]
    workspace=_path(manifest["workspace"])
    assert workspace.is_dir()
    def identity(value):
        path=Path(value["path"])
        if not path.is_absolute():
            path=workspace/path
        return str(_path(str(path))),value["bytes"],value["sha256"]
    assert identity(manifest["captured_reader"]) == identity(captured)
    assert (captured["bytes"],captured["sha256"]) == (104946,"682bdf91cad1669e7fa37311f4d2be98bf200fdde643e16c4330a74d0596cc7b")
    old_source=_pin(manifest["captured_reader"])
    new_source=_pin(manifest["qualified_reader"])
    assert new_source == actual and old_source != actual
    assert actual.stat().st_size == manifest["qualified_reader"]["bytes"]
    assert hashlib.sha256(actual.read_bytes()).hexdigest() == manifest["qualified_reader"]["sha256"]
    checked_provenance=_json(_pin(manifest["provenance"]),2*1024*1024)
    assert _typed(checked_provenance) == _typed(provenance)
    capture=_json(_pin(manifest["capture"]),2*1024*1024)
    assert capture["state"] == "passed" and capture["kind"] == "p06i.installed-ui-capture.001"
    assert capture["complete_case_coverage"] is True and capture["selected_case_ids"] == list(CASES)
    assert identity(capture["provenance"]) == identity(manifest["provenance"])
    assert capture["cases"] == provenance["cases"]
    failed=_json(_pin(manifest["failed_reader_run"]),2*1024*1024)
    assert failed["state"] == "failed" and failed["id"] == "p06i.paired-qualification.001"
    assert identity(failed["capture"]) == identity(manifest["capture"])
    assert identity(failed["provenance"]) == identity(manifest["provenance"])
    assert [(item["name"],item["state"],item["exit_code"]) for item in failed["processes"]] == [
        ("capture24","passed",0),("reader24","failed",1)]
    assert failed["owned_job_cleanup_verified"] is True
    origins=_json(_pin(manifest["failed_reader_origins"]),128*1024)
    assert origins["state"] == "failed" and origins["pytest_exit_code"] == 1 and origins["product_or_numpy_imported"] is False
    assert _path(origins["reader_source"]["path"]) == actual
    assert (origins["reader_source"]["bytes"],origins["reader_source"]["sha256"]) == (captured["bytes"],captured["sha256"])
    report_path=_pin(manifest["failed_reader_report"],4*1024*1024)
    report=report_path.read_bytes()
    assert b"<!DOCTYPE" not in report and b"<!ENTITY" not in report
    for index,value in enumerate(origins["pytest_arguments"]):
        if value == "--junitxml":
            assert _path(origins["pytest_arguments"][index+1]) == report_path
            break
    else:
        raise AssertionError("Failed reader did not bind the retained JUnit report")
    tests=list(ET.fromstring(report).iter("testcase"))
    assert [item.attrib["name"] for item in tests] == list(EXPECTED_COLLECTION_IDS)
    assert not any(item.find(tag) is not None for item in tests for tag in ("error","skipped"))
    assert [item.attrib["name"] for item in tests if item.find("failure") is not None] == [
        "test_installed_scientific_orientation[O09-imgui_cpp]","test_installed_scientific_orientation[O09-imgui_python]"]
    before_tree,after_tree=ast.parse(old_source.read_bytes()),ast.parse(actual.read_bytes())
    before_functions={node.name:node for node in before_tree.body if isinstance(node,ast.FunctionDef)}
    after_functions={node.name:node for node in after_tree.body if isinstance(node,ast.FunctionDef)}
    node_dump=lambda node:ast.dump(node,include_attributes=False)
    changed=sorted(name for name in before_functions if node_dump(before_functions[name]) != node_dump(after_functions[name]))
    added=sorted(set(after_functions)-set(before_functions))
    assert set(before_functions) <= set(after_functions)
    assert changed == ["_inputs","_workflow_rebind_case"] and added == ["_reader_source_repair"]
    globals_before=[node_dump(node) for node in before_tree.body if not isinstance(node,(ast.FunctionDef,ast.Import,ast.ImportFrom))]
    globals_after=[node_dump(node) for node in after_tree.body if not isinstance(node,(ast.FunctionDef,ast.Import,ast.ImportFrom))]
    assert globals_before == globals_after
    def imports(tree):
        return sorted((node.module,tuple((name.name,name.asname) for name in node.names)) if isinstance(node,ast.ImportFrom)
                      else ("",tuple((name.name,name.asname) for name in node.names))
                      for node in tree.body if isinstance(node,(ast.Import,ast.ImportFrom)))
    assert set(imports(after_tree))-set(imports(before_tree)) == {
        ("",(("ast",None),)),("",(("base64",None),)),("",(("xml.etree.ElementTree","ET"),))}
    assert set(imports(before_tree)) <= set(imports(after_tree))
    comparison=_json(_pin(manifest["source_comparison"]),128*1024)
    assert comparison["state"] == "source_ast_preservation_checked" and comparison["changed_functions"] == changed
    assert comparison["added_functions"] == added and comparison["preserved_case_contexts"] == 22
    _pin(comparison["before"]); _pin(comparison["after"])
    assert identity(comparison["before"])[1:] == identity(manifest["captured_reader"])[1:]
    assert identity(comparison["after"]) == identity(manifest["qualified_reader"])
    hold=_json(_pin(manifest["repair_hold"]),128*1024)
    assert hold["state"] == "written_unexecuted" and hold["writes_stopped"] is True
    assert identity(hold["owned_source"]) == identity(manifest["qualified_reader"])
    _pin(hold["preimage"])
    assert identity(hold["preimage"])[1:] == identity(manifest["captured_reader"])[1:]
    assert identity(hold["source_comparison"]) == identity(manifest["source_comparison"])


@lru_cache(maxsize=1)
def _inputs():
    configured = os.environ.get("IMAGEJXX_SCIENTIFIC_ORIENTATION_UI_CASE_ROOT")
    assert configured, "Required orientation24 raw capture root is missing; no acceptance without captures"
    root = _path(configured)
    assert root.is_dir()
    provenance = _json(root / "provenance.json", 2 * 1024 * 1024)
    assert provenance["schema_version"] == 1
    assert provenance["kind"] == "p06i.scientific-orientation-ui-reader-inputs.001"
    assert set(provenance["contexts"]) == set(CONTEXTS)
    selected = provenance["selected_case_ids"]
    assert type(selected) is list and selected in (list(CASES[:6]), list(CASES))
    assert type(provenance["complete_case_coverage"]) is bool
    assert provenance["complete_case_coverage"] is (selected == list(CASES))
    contract_pin = provenance["adopted_contract"]
    assert contract_pin["bytes"] == CONTRACT_BYTES and contract_pin["sha256"] == CONTRACT_SHA256
    contract = _json(_pin(contract_pin), 128 * 1024)
    assert [item["id"] for item in contract["proposed_membership"]["paired_cases"]] == list(CASES)
    assert contract["proposed_membership"]["contexts"] == list(CONTEXTS)
    sources = provenance["sources"]
    assert type(sources) is list and len(SOURCE_ROLES) <= len(sources) <= 128
    roles = {}
    for source in sources:
        role = source["role"]
        assert type(role) is str and role and role not in roles
        _pin(source)
        if role in SOURCE_ROLES:
            assert source["logical_path"].replace("\\", "/") == SOURCE_ROLES[role]
        roles[role] = source
    assert set(SOURCE_ROLES) <= set(roles)
    for role, actual in (("reader", Path(__file__).resolve()),
                         ("observation_transport", Path(_transport.__file__).resolve())):
        identity=(actual.stat().st_size,hashlib.sha256(actual.read_bytes()).hexdigest())
        if role == "reader" and identity != (roles[role]["bytes"],roles[role]["sha256"]):
            _reader_source_repair(actual,roles[role],provenance)
        else:
            assert identity == (roles[role]["bytes"],roles[role]["sha256"])
    source_set = _source_set(sources)
    assert provenance["source_set_sha256"] == source_set
    descriptors = provenance["cases"]
    assert [(item["id"], item["context"]) for item in descriptors] == [
        (case, context) for case in selected for context in CONTEXTS]
    for descriptor in descriptors:
        raw = _pin(descriptor["raw"], MAX_REPORT)
        index = _pin(descriptor["case_index"], 128 * 1024)
        assert raw.name == "case.json" and index.name == "index.json" and raw.parent == index.parent
        assert root in raw.parents and descriptor["raw"]["bytes"] + descriptor["case_index"]["bytes"] <= MAX_ARCHIVE
        if descriptor["context"] == "imgui_python":
            _pin(descriptor["launcher_origin"], 128 * 1024)
    for context in CONTEXTS:
        binding = provenance["contexts"][context]
        installed = _path(binding["installed_root"])
        assert installed.is_dir()
        for role in ("executable", "producer_artifact", "native_artifact"):
            artifact = _pin(binding[role])
            if context == "imgui_cpp" or role != "executable":
                assert installed in artifact.parents
        build = _json(_pin(binding["build_receipt"]), 2 * 1024 * 1024)
        assert build["schema_version"] == 1
        assert build["kind"] == "p06i.scientific-orientation-source-product-binding.001"
        assert build["source_set_sha256"] == binding["source_set_sha256"] == source_set
        assert build["sources"] == sources
        assert build["adopted_contract"] == contract_pin
        assert build["selected_case_ids"] == selected and build["complete_case_coverage"] is provenance["complete_case_coverage"]
        passed = _json(_pin(build["passed_build_receipt"]), 2 * 1024 * 1024)
        freeze = _json(_pin(build["source_freeze"]), 2 * 1024 * 1024)
        explicit = _json(_pin(build["explicit_product_bindings"]), 2 * 1024 * 1024)
        assert explicit["schema_version"] == 1 and explicit["kind"] == "p06i.installed-ui-capture-bindings.001"
        assert explicit["kind_selection"] == "scientific-orientation" and explicit["adopted_contract"] == contract_pin
        assert explicit["build_receipt"] == build["passed_build_receipt"] and explicit["source_freeze"] == build["source_freeze"]
        assert passed["state"] == "passed" and passed["source_freeze"] == build["source_freeze"]
        assert passed["built_products"] == explicit["built_products"] and explicit["products"] == build["products"]
        assert freeze["schema_version"] == 1 and type(freeze["sources"]) is list
        frozen = {}
        for item in freeze["sources"]:
            logical = item["logical_path"]
            assert type(logical) is str and logical not in frozen
            frozen[logical] = item
        for source in sources:
            original = frozen[source["logical_path"]]
            assert (original["bytes"], original["sha256"]) == (source["bytes"], source["sha256"])
            _pin(original)
        for artifact in explicit["built_products"].values():
            _pin(artifact)
        for role in ("executable", "producer_artifact", "native_artifact"):
            assert build["products"][context][role] == binding[role]
        assert 0 < _number(binding["elapsed_seconds"]) <= 60
    return root, provenance


def _origins(row, descriptor, provenance):
    runtime = row["runtime_before"]
    assert runtime == row["runtime_after"]
    _integer(runtime["process_id"], 1)
    assert runtime["pointer_bits"] == 64
    assert type(runtime["imgui"]) is str and runtime["imgui"]
    assert runtime["producer_source"].replace("\\", "/").endswith("/" + SOURCE_ROLES["producer"])
    context = row["context"]
    binding = provenance["contexts"][context]
    for role in ("executable", "producer_artifact", "native_artifact"):
        assert runtime[role] == {key: binding[role][key] for key in ("path", "bytes", "sha256")}
        _pin(runtime[role])
    if context == "imgui_cpp":
        assert runtime["executable"] == runtime["producer_artifact"]
    else:
        origin = _json(_pin(descriptor["launcher_origin"]), 128 * 1024)
        assert origin["schema_version"] == 1
        assert origin["kind"] == "p06i.scientific-orientation-python-launcher-origin.001"
        assert (origin["case_id"], origin["context"], origin["process_id"]) == (row["case_id"], context, runtime["process_id"])
        assert origin["before"] == origin["after"]
        for role in ("executable", "producer_artifact", "native_artifact"):
            assert origin["before"][role] == binding[role]
        assert origin["source_set_sha256"] == provenance["source_set_sha256"]
        build = _json(_pin(binding["build_receipt"]), 2 * 1024 * 1024)
        explicit = _json(_pin(build["explicit_product_bindings"]), 2 * 1024 * 1024)
        assert origin["modules_before"] == origin["modules_after"] == explicit["installed_modules"]
        for module in origin["modules_before"].values():
            module_path = _pin(module)
            assert _path(binding["installed_root"]) in module_path.parents
        assert origin["engine_before"] == origin["engine_after"] == binding["executable"]
        assert origin["numpy_before"] == origin["numpy_after"] == explicit["numpy"]["module"]
        _pin(origin["numpy_before"])


def _load(case, context):
    _, provenance = _inputs()
    descriptors = [item for item in provenance["cases"] if (item["id"], item["context"]) == (case, context)]
    assert len(descriptors) == 1, "Missing required actual orientation capture: " + case + "-" + context
    descriptor = descriptors[0]
    raw_path = _pin(descriptor["raw"], MAX_REPORT)
    index = _json(_pin(descriptor["case_index"]), 128 * 1024)
    assert index["schema_version"] == 1 and index["contract_id"] == CONTRACT
    assert (index["case_id"], index["context"]) == (case, context)
    assert index["report"] == {"member": "case.json", "bytes": descriptor["raw"]["bytes"], "sha256": descriptor["raw"]["sha256"]}
    assert index["observation_state"] == "observed" and index["acceptance_claim"] is False
    row = _transport.decode_actual_observations(_json(raw_path), max_expanded_bytes=64 * 1024 * 1024)
    assert row["schema_version"] == 1 and row["schema"] == "imagejxx.scientific-orientation-ui-observations"
    assert row["contract_id"] == CONTRACT and (row["case_id"], row["context"]) == (case, context)
    assert row["observation_state"] == "observed" and row["acceptance_claim"] is False
    assert not {"expected", "PASS", "pass", "passed", "outcome", "failure"}.intersection(row)
    assert row["declared_case_membership"] == list(CASES)
    assert row["implemented_cases"] == index["implemented_cases"] == list(IMPLEMENTED_CASES)
    assert row["pending_cases"] == index["pending_cases"] == list(PENDING_CASES)
    assert row["limits"] == dict(frames_per_case=180, seconds_per_case=15., report_bytes_per_case=MAX_REPORT,
                                 native_archive_bytes_per_case=MAX_ARCHIVE, live_documents=16)
    assert type(row["frame_count"]) is int and 1 <= row["frame_count"] == len(row["frames"]) <= MAX_FRAMES
    assert 0 < _number(row["elapsed_seconds"]) <= MAX_SECONDS
    _origins(row, descriptor, provenance)
    return row


def _typed(value):
    if type(value) is dict:
        return {key: _typed(item) for key, item in value.items()}
    if type(value) is list:
        return [_typed(item) for item in value]
    if type(value) is float:
        return ("float64", struct.pack("<d", value).hex())
    return (type(value).__name__, value)


def _near(actual, expected):
    if type(expected) is dict:
        assert type(actual) is dict and set(actual) == set(expected)
        for key, item in expected.items():
            _near(actual[key], item)
    elif type(expected) in (list, tuple):
        assert type(actual) is list and len(actual) == len(expected)
        for value, item in zip(actual, expected):
            _near(value, item)
    elif type(expected) is str:
        assert type(actual) is str and actual == expected
    else:
        assert abs(_number(actual) - expected) <= 1e-12


def _control(report, selector):
    if selector.startswith("dialog."):
        return report["dialog"]["controls"][selector.removeprefix("dialog.")]
    if selector.startswith("document."):
        matches = [item for item in report["document_tabs"] if item["document_id"] == int(selector.removeprefix("document."))]
        assert len(matches) == 1
        return matches[0]
    if selector.startswith("error_modal."):
        return report["error_modal"]["controls"][selector.removeprefix("error_modal.")]
    if selector.startswith("close_prompt."):
        return report["close_prompt"]["controls"][selector.removeprefix("close_prompt.")]
    if selector.startswith("gaussian."):
        name = selector.removeprefix("gaussian.")
        panel = report["gaussian_panel"]
        return dict(panel["control_states"][name],point=panel["controls"][name],rect=panel["control_rects"][name])
    return report["host_controls"][selector]


def _physical(row):
    frames = row["frames"]
    assert [item["ordinal"] for item in frames] == list(range(1, len(frames) + 1))
    held, buttons, position, sessions = set(), [False] * 3, None, []
    for frame in frames:
        report = frame["report"]
        session, actual = report["session"], report["input_frame"]
        assert session["open"] is True
        assert _integer(session["context_id"], 1) == session["current_context_id"]
        sessions.append(session)
        events = frame["requested_events"]
        assert type(events) is list and actual["raw_inputs"] == events
        for event in events:
            kind = event["type"]
            if kind == "position":
                assert set(event) == {"type", "space", "point"} and event["space"] == "screen"
                assert len(event["point"]) == 2
                position = [_number(value) for value in event["point"]]
            elif kind == "button":
                assert set(event) == {"type", "name", "down"} and event["name"] in ("left", "right", "middle")
                buttons[("left", "right", "middle").index(event["name"])] = _boolean(event["down"])
            elif kind in ("key", "keys"):
                if kind == "key":
                    assert set(event) == {"type", "name", "down"}
                    atoms = [{"name": event["name"], "down": event["down"]}]
                else:
                    assert set(event) == {"type", "keys"}
                    atoms = event["keys"]
                assert 1 <= len(atoms) <= 16 and len({item["name"] for item in atoms}) == len(atoms)
                for atom in atoms:
                    assert set(atom) == {"name", "down"} and atom["name"] in PHYSICAL_KEYS
                    (held.add if _boolean(atom["down"]) else held.discard)(atom["name"])
            elif kind == "text":
                assert set(event) == {"type", "value"} and type(event["value"]) is str
                assert "\0" not in event["value"]
                assert actual["input_text"]["id"] == actual["focus"]["active_id"] != 0
            elif kind == "wheel":
                assert set(event) == {"type", "x", "y"}
                _number(event["x"]); _number(event["y"])
            else:
                raise AssertionError("Undeclared actual physical input: " + str(kind))
        assert set(actual["keys_down"]) == held and actual["mouse_down"] == buttons
        if position is not None:
            assert actual["mouse_position"] == position
        assert actual["modifiers"] == {"ctrl": "Ctrl" in held, "shift": "Shift" in held,
                                       "alt": "Alt" in held, "super": "Super" in held}
        _collection(frame["native_state"], full=False, first_slice=row["case_id"] in CASES[:6])
    assert not held and buttons == [False] * 3
    assert len({item["context_id"] for item in sessions}) == len({item["session_id"] for item in sessions}) == 1
    for before, after in zip(sessions, sessions[1:]):
        assert after["imgui_frame"] == before["imgui_frame"] + 1 and after["imgui_time"] > before["imgui_time"]
    clicks = set()
    for observation in row["control_observations"]:
        ordinal = _integer(observation["before_frame_ordinal"], 1, len(frames)-3)
        selector = observation["selector"]
        control = _control(frames[ordinal-1]["report"], selector)
        assert _typed(control) == _typed(observation["control"])
        _integer(control["id"], 1)
        assert _boolean(control["enabled"]) is (not _boolean(observation["disabled_attempt"]))
        point, rect = observation["position"], control["rect"]
        assert len(point) == 2 and len(rect) == 4
        assert point == [math.floor(_number(value)) for value in control["point"]]
        assert rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]
        assert [item["requested_events"] for item in frames[ordinal:ordinal+3]] == [
            [{"type": "position", "space": "screen", "point": point}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}],
        ]
        positioned = _control(frames[ordinal]["report"], selector)
        assert positioned["id"] == control["id"] and positioned["rect"] == rect
        assert positioned["enabled"] is control["enabled"]
        covered = set(range(ordinal+1, ordinal+4))
        assert not clicks.intersection(covered)
        clicks.update(covered)
    for frame in frames:
        if any(event["type"] == "button" for event in frame["requested_events"]):
            assert frame["ordinal"] in clicks
    for discovery in row["control_discovery_observations"]:
        ordinal = _integer(discovery["frame_ordinal"], 2, len(frames))
        before, after = discovery["before_control"], discovery["after_control"]
        selector = discovery["selector"]
        assert _typed(before) == _typed(_control(frames[ordinal-2]["report"], selector))
        assert _typed(after) == _typed(_control(frames[ordinal-1]["report"], selector))
        assert before["id"] != after["id"] or before["rect"] != after["rect"] or before["enabled"] != after["enabled"]
        assert frames[ordinal-1]["requested_events"] == [{"type": "position", "space": "screen", "point": discovery["position"]}]
    opened, closed = row["lifecycle"]
    assert opened["open"] is True and closed["open"] is False
    assert opened["session_id"] == closed["session_id"] == sessions[0]["session_id"]
    assert closed["context_id"] == closed["current_context_id"] == 0


def _state(row, label):
    assert label in row["states"], "Missing actual checkpoint: " + label
    state = row["states"][label]
    ordinal = _integer(state["frame_ordinal"], 1, len(row["frames"]))
    frame = row["frames"][ordinal-1]
    assert frame["label"] == label
    _collection(state, first_slice=row["case_id"] in CASES[:6])
    assert _typed(state["dialog"]) == _typed(frame["report"]["dialog"])
    return state, frame["report"]


def _owner(state, owner_id):
    matches = [document for document in state["documents"] if document["document_id"] == owner_id]
    assert len(matches) == 1
    return matches[0]


def _collection(state, *, full=True, first_slice=True):
    ids = state["document_ids"]
    assert type(ids) is list and len(ids) == len(set(ids)) <= 16
    assert ids == state["facade_ids"] == [document["document_id"] for document in state["documents"]]
    assert state["active_document_id"] in ids or (not ids and state["active_document_id"] == 0)
    _boolean(state["busy"]); _boolean(state["recording"])
    assert type(state["workflow"]) in (list,dict) and type(state["results"]) is list and type(state["window_error"]) is str
    assert set(state["usage"]) == set(ZERO_USAGE)
    if first_slice:
        assert state["busy"] is False and state["recording"] is True
        assert type(state["workflow"]) is list and state["results"] == [] and state["window_error"] == ""
        assert state["usage"] == ZERO_USAGE
    for value in state["usage"].values():
        _integer(value)
    for document in state["documents"]:
        _integer(document["document_id"], 1)
        # A data identity is an owner-local save/history generation. Fresh
        # independently owned outputs legitimately start at generation zero.
        _integer(document["data_identity"])
        shape = document["shape"]
        assert type(shape) is list and len(shape) == 6
        for value in shape:
            _integer(value,1)
        assert type(document["dirty"]) is bool
        assert set(document["revisions"]) == {"pixels", "metadata", "selection", "display"}
        for value in document["revisions"].values():
            _integer(value)
        display = document["display"]
        assert set(display) == {"t","c","crosshair","orthogonal","linked_zoom","zoom","plane_zoom","pan","revision","properties"}
        _integer(display["t"],0,shape[0]-1); _integer(display["c"],0,shape[1]-1)
        assert len(display["crosshair"]) == 3
        for value, extent in zip(display["crosshair"],(shape[4],shape[3],shape[2])):
            _integer(value,0,extent-1)
        _boolean(display["orthogonal"]); _boolean(display["linked_zoom"])
        assert _number(display["zoom"]) >= 0 and len(display["plane_zoom"]) == 3
        for value in display["plane_zoom"]:
            assert _number(value) >= 0
        assert len(display["pan"]) == 3
        for point in display["pan"]:
            assert len(point) == 2
            for value in point:
                _number(value)
        _integer(display["revision"])
        assert type(display["properties"]) is dict
        history = document["history"]
        assert set(history) == {"undo_steps", "redo_steps", "step_limit", "pixel_budget", "logical_bytes",
                                "current_bytes", "retained_bytes", "obsolete_bytes", "retained_planes", "exact"}
        for key, value in history.items():
            if key == "exact":
                _boolean(value)
            else:
                _integer(value)
        if full:
            _planes(document)


def _planes(document):
    shape = document["shape"]
    assert type(shape) is list and len(shape) == 6 and all(type(value) is int and value > 0 for value in shape)
    assert shape[5] in (1, 3, 4)
    codes = {"uint8": "B", "uint16": "H", "float32": "f", "float64": "d"}
    code = codes[document["dtype"]]
    assert type(document["planes"]) is list and len(document["planes"]) == math.prod(shape[:3])
    count = math.prod(shape[3:])
    result = {}
    for index, plane in enumerate(document["planes"]):
        t, remainder = divmod(index,shape[1]*shape[2])
        c, z = divmod(remainder,shape[2])
        position = (t,c,z)
        assert plane["tcz"] == list(position) and all(type(value) is int for value in plane["tcz"])
        for key, width in (("declared_hex", struct.calcsize(code)), ("storage_f64le_hex", 8)):
            text = plane[key]
            assert type(text) is str and re.fullmatch("[0-9a-f]*", text)
            assert len(text) == 2*count*width
        for key in ("image_address", "buffer_address"):
            address = plane[key]
            assert type(address) is str and re.fullmatch("0x[0-9a-f]+", address) and int(address,16) > 0
        result[position] = plane
    return result


@lru_cache(maxsize=64)
def _source_plane(t, c, z):
    values = [40000 + (97*t + 31*c + 11*z + 5*y + 3*x) % 251 for y in range(7) for x in range(9)]
    return struct.pack("<" + "H"*len(values), *values).hex(), struct.pack("<" + "d"*len(values), *values).hex()


def _literal_source(document):
    assert document["shape"] == SHAPE and document["dtype"] == "uint16" and document["sample_policy"] == "finite_only"
    _near(document["calibration"], CALIBRATION)
    for position, plane in _planes(document).items():
        declared, backing = _source_plane(*position)
        assert plane["declared_hex"] == declared and plane["storage_f64le_hex"] == backing
    assert document["metadata"]["title"] == "Scientific orientation source"
    assert _typed(document["metadata"]["properties"]) == _typed({"source_only": {"nested": [None, True, 7, "retained"]}, "revision_probe": 7})
    assert _typed(document["metadata"]["extensions"]) == _typed({"future_annotation": {"version": 37, "opaque": [3,1,4]}})
    assert _typed(document["metadata"]["record_layout"]) == _typed({"source_presence": True})
    rectangle = dict(kind="rectangle",points=[[1.,1.],[4.,3.]],width=1.)
    previous = dict(kind="line",points=[[0.,0.],[2.,2.]],width=1.)
    _near(document["selection"],rectangle); _near(document["previous_selection"],previous)
    expected_rois = [dict(name="explicit zero",selection=dict(kind="rectangle",points=[[1.,1.],[3.,3.]],width=1.),t=0,c=0,z=0,attributes={}),
                     dict(name="wildcard",selection=dict(kind="point",points=[[2.,2.]],width=1.),t=None,c=None,z=1,attributes={})]
    assert len(document["rois"]) == 2 and document["spectrum_present"] is False
    for actual, expected in zip(document["rois"],expected_rois):
        assert set(actual) == set(expected) and actual["name"] == expected["name"] and actual["attributes"] == {}
        _near(actual["selection"],expected["selection"])
        assert [actual[key] for key in ("t","c","z")] == [expected[key] for key in ("t","c","z")]
    assert all(type(document["rois"][0][name]) is int for name in ("t", "c", "z"))
    assert type(document["rois"][1]["z"]) is int
    _near(document["overlays"],[dict(name="source overlay",kind="point",points=[[6.,4.]])])
    assert document["history"]["undo_steps"] == document["history"]["redo_steps"] == 1
    assert document["dirty"] is False


def _unchanged(before, after, *, display=True):
    for key in ("document_id", "shape", "dtype", "sample_policy", "calibration", "metadata", "selection",
                "previous_selection", "rois", "overlays", "spectrum_present", "data_identity", "dirty", "history", "planes"):
        assert _typed(after[key]) == _typed(before[key]), "Source changed: " + key
    for key in ("pixels", "metadata", "selection"):
        assert after["revisions"][key] == before["revisions"][key]
    if display:
        assert _typed(after["display"]) == _typed(before["display"])
        assert after["revisions"]["display"] == before["revisions"]["display"]
    else:
        for key in ("crosshair", "orthogonal", "linked_zoom", "zoom", "plane_zoom", "pan", "properties"):
            assert _typed(after["display"][key]) == _typed(before["display"][key])


def _world(calibration, xyz):
    return [calibration["origin"][row] + sum(calibration["direction"][3*row+axis] *
            calibration["spacing"][axis] * xyz[axis] for axis in range(3)) for row in range(3)]


def _request(spec):
    _, plane, first, last, step, reverse, flip, transpose = spec
    return dict(plane=plane, first=first, last=last, step=step, t=1, c=2,
                max_working_bytes=CAP, reverse_order=reverse, flip_vertical=flip, transpose=transpose)


def _derived(request):
    base = AXES[request["plane"]]
    extent = (9, 7, 5)
    reverse, flip, transpose = (request[key] for key in ("reverse_order", "flip_vertical", "transpose"))
    selected = list(range(request["first"], request["last"]+1, request["step"]))
    if reverse:
        selected.reverse()
    final = (base[1], base[0], base[2]) if transpose else base
    signs = (-1 if flip else 1, 1, -1 if reverse else 1) if transpose else (1, -1 if flip else 1, -1 if reverse else 1)
    shape = [1,1,len(selected),extent[final[1]],extent[final[0]],1]
    source_origin = [0,0,0]
    source_origin[base[1]] = extent[base[1]]-1 if flip else 0
    source_origin[base[2]] = selected[0]
    calibration = dict(
        spacing=[CALIBRATION["spacing"][axis]*(request["step"] if index == 2 else 1) for index, axis in enumerate(final)],
        origin=_world(CALIBRATION,source_origin),
        direction=[signs[col]*CALIBRATION["direction"][3*row+axis] for row in range(3) for col, axis in enumerate(final)],
        unit="um", frame_interval=.75, time_origin=3., time_unit="min")
    geometry = dict(origin=calibration["origin"],
                    horizontal_step=[calibration["direction"][3*row]*calibration["spacing"][0] for row in range(3)],
                    vertical_step=[calibration["direction"][3*row+1]*calibration["spacing"][1] for row in range(3)],
                    horizontal_axis="XYZ"[final[0]], vertical_axis="XYZ"[final[1]], unit="um")
    plane_samples = extent[base[0]]*extent[base[1]]
    working = 8*(plane_samples*(len(selected)+1) + 7*9 + (0 if request["plane"] == "XY" else extent[base[0]]))
    return dict(shape=shape,calibration=calibration,geometry=geometry,source_axes=list(base),
                plane_name=request["plane"],working_bytes=working), selected, final, signs


def _record(record, source, request):
    assert set(record) == {"schema", "version", "operation", "source", "request", "derived", "destination"}
    assert record["schema"] == "imagejxx.axis-extraction-command" and type(record["version"]) is int and record["version"] == 2
    assert record["operation"] == "axis_range_orientation" and record["destination"] == "new_document"
    assert _typed(record["request"]) == _typed(request)
    for key in ("first", "last", "step", "t", "c", "max_working_bytes"):
        _integer(record["request"][key], 1 if key in ("step", "max_working_bytes") else 0)
    for key in ("reverse_order", "flip_vertical", "transpose"):
        _boolean(record["request"][key])
    assert record["source"] == dict(document_id=source["document_id"],shape=SHAPE,pixel_type="uint16",sample_policy="finite_only",
        revisions={key: source["revisions"][key] for key in ("pixels", "metadata")},calibration=source["calibration"])
    assert type(record["source"]["document_id"]) is int
    assert all(type(value) is int for value in record["source"]["shape"])
    for value in record["source"]["revisions"].values():
        _integer(value)
    _near(record["source"]["calibration"],CALIBRATION)
    expected, _, _, _ = _derived(request)
    derived = record["derived"]
    assert set(derived) == set(expected)
    for key in ("shape", "source_axes"):
        assert _typed(derived[key]) == _typed(expected[key])
    assert derived["plane_name"] == expected["plane_name"]
    assert _integer(derived["working_bytes"], 1) == expected["working_bytes"]
    _near(derived["calibration"],expected["calibration"])
    _near(derived["geometry"],expected["geometry"])


def _profile_pixels(document, profile):
    shape, dtype = SOURCE_PROFILES[profile]
    assert document["shape"] == shape and document["dtype"] == dtype
    assert document["sample_policy"] == "finite_only"
    _near(document["calibration"],CALIBRATION)
    code = "H" if dtype == "uint16" else "B"
    for (t,c,z), plane in _planes(document).items():
        values = []
        for y in range(7):
            for x in range(9):
                for sample in range(shape[5]):
                    q = (97*t + 31*c + 11*z + 5*y + 3*x + sample) % 251
                    values.append(40000+q if dtype == "uint16" else q)
        assert plane["declared_hex"] == struct.pack("<"+code*len(values),*values).hex()
        assert plane["storage_f64le_hex"] == struct.pack("<"+"d"*len(values),*values).hex()


def _layout_for(request, profile, *, operation="axis_range_orientation"):
    shape, _ = SOURCE_PROFILES[profile]
    base = AXES[request["plane"]]
    extent = (shape[4],shape[3],shape[2])
    oriented = operation == "axis_range_orientation"
    reverse = request["reverse_order"] if oriented else False
    flip = request["flip_vertical"] if oriented else False
    transpose = request["transpose"] if oriented else False
    if operation == "axis_plane_export":
        selected, step = [request["coordinate"]], 1
    else:
        step = request["step"]
        selected = list(range(request["first"],request["last"]+1,step))
    if reverse:
        selected.reverse()
    final = (base[1],base[0],base[2]) if transpose else base
    signs = (-1 if flip else 1,1,-1 if reverse else 1) if transpose else (1,-1 if flip else 1,-1 if reverse else 1)
    source_origin = [0,0,0]
    source_origin[base[1]] = extent[base[1]]-1 if flip else 0
    source_origin[base[2]] = selected[0]
    output_shape = [1,1,len(selected),extent[final[1]],extent[final[0]],shape[5]]
    calibration = dict(spacing=[CALIBRATION["spacing"][axis]*(step if column == 2 else 1) for column,axis in enumerate(final)],
        origin=_world(CALIBRATION,source_origin),
        direction=[signs[column]*CALIBRATION["direction"][3*row+axis] for row in range(3) for column,axis in enumerate(final)],
        unit="um",frame_interval=.75,time_origin=2.25+.75*request["t"],time_unit="min")
    geometry = dict(origin=calibration["origin"],
        horizontal_step=[calibration["direction"][3*row]*calibration["spacing"][0] for row in range(3)],
        vertical_step=[calibration["direction"][3*row+1]*calibration["spacing"][1] for row in range(3)],
        horizontal_axis="XYZ"[final[0]],vertical_axis="XYZ"[final[1]],unit="um")
    plane_samples = extent[base[0]]*extent[base[1]]*shape[5]
    working = 8*(plane_samples*len(selected)+7*9*shape[5]+(0 if request["plane"] == "XY" else extent[base[0]]*shape[5]))
    if oriented:
        working += 8*plane_samples
    return dict(shape=output_shape,calibration=calibration,geometry=geometry,source_axes=list(base),
                plane_name=request["plane"],working_bytes=working), selected, final


def _wire_record(record, source, request, profile, *, operation="axis_range_orientation"):
    _profile_pixels(source,profile)
    assert set(record) == {"schema","version","operation","source","request","derived","destination"}
    assert record["schema"] == "imagejxx.axis-extraction-command"
    assert type(record["version"]) is int and record["version"] == (2 if operation == "axis_range_orientation" else 1)
    assert record["operation"] == operation and record["destination"] == "new_document"
    assert _typed(record["request"]) == _typed(request)
    assert record["source"] == dict(document_id=source["document_id"],shape=source["shape"],pixel_type=source["dtype"],
        sample_policy="finite_only",revisions={key:source["revisions"][key] for key in ("pixels","metadata")},calibration=source["calibration"])
    _integer(record["source"]["document_id"],1)
    for value in record["source"]["shape"]:
        _integer(value,1)
    for value in record["source"]["revisions"].values():
        _integer(value)
    _near(record["source"]["calibration"],CALIBRATION)
    expected, _, _ = _layout_for(request,profile,operation=operation)
    assert set(record["derived"]) == set(expected)
    for key in ("shape","source_axes","plane_name","working_bytes"):
        assert _typed(record["derived"][key]) == _typed(expected[key])
    _near(record["derived"]["calibration"],expected["calibration"])
    _near(record["derived"]["geometry"],expected["geometry"])


def _literal_output_pixels(document, request, profile, *, operation="axis_range_orientation"):
    source_shape, dtype = SOURCE_PROFILES[profile]
    expected, selected, final = _layout_for(request,profile,operation=operation)
    assert document["shape"] == expected["shape"] and document["dtype"] == dtype
    assert document["sample_policy"] == "finite_only"
    _near(document["calibration"],expected["calibration"])
    base = AXES[request["plane"]]
    transpose = request.get("transpose",False)
    flip = request.get("flip_vertical",False)
    code = "H" if dtype == "uint16" else "B"
    for (t,c,k), plane in _planes(document).items():
        assert t == c == 0
        values = []
        for v in range(document["shape"][3]):
            for u in range(document["shape"][4]):
                horizontal, vertical = (v,u) if transpose else (u,v)
                xyz = [0,0,0]
                xyz[base[0]] = horizontal
                xyz[base[1]] = (9,7,5)[base[1]]-1-vertical if flip else vertical
                xyz[base[2]] = selected[k]
                for sample in range(source_shape[5]):
                    q = (97*request["t"]+31*request["c"]+11*xyz[2]+5*xyz[1]+3*xyz[0]+sample)%251
                    values.append(40000+q if dtype == "uint16" else q)
                _near(_world(document["calibration"],[u,v,k]),_world(CALIBRATION,xyz))
        assert plane["declared_hex"] == struct.pack("<"+code*len(values),*values).hex()
        assert plane["storage_f64le_hex"] == struct.pack("<"+"d"*len(values),*values).hex()
    return expected, selected, final


def _new_request(plane="XY", first=1, last=4, step=2, *, t=1, c=2, flags=(False,False,False)):
    return dict(plane=plane,first=first,last=last,step=step,t=t,c=c,max_working_bytes=CAP,
                reverse_order=flags[0],flip_vertical=flags[1],transpose=flags[2])


def _same_data(before, after, *, display=True):
    for key in ("document_id","shape","dtype","sample_policy","calibration","metadata","selection",
                "previous_selection","rois","overlays","spectrum_present","data_identity","dirty","history","planes"):
        assert _typed(before[key]) == _typed(after[key]), "Unexpected owner mutation: "+key
    for key in ("pixels","metadata","selection"):
        assert _typed(before["revisions"][key]) == _typed(after["revisions"][key])
    if display:
        assert _typed(before["display"]) == _typed(after["display"])
        assert _typed(before["revisions"]["display"]) == _typed(after["revisions"]["display"])


def _same_collection(before, after, *, error=True):
    for key in ("document_ids","facade_ids","active_document_id","busy","recording","workflow","results","usage"):
        assert _typed(before[key]) == _typed(after[key]), "Unexpected collection mutation: "+key
    if error:
        assert before["window_error"] == after["window_error"]
    for document in before["documents"]:
        _same_data(document,_owner(after,document["document_id"]))


def _api(row, label, *, accepted=True, category=None):
    matches = [item for item in row["api_observations"] if item["label"] == label]
    assert len(matches) == 1, "Missing/duplicate disclosed actual API stimulus: "+label
    event = matches[0]
    assert event["scope"] == "Disclosed API stimulus/preparation, not physical UI execution"
    assert _boolean(event["accepted"]) is accepted
    _integer(event["before_frame_ordinal"],1,len(row["frames"]))
    _collection(event["before"],first_slice=False); _collection(event["after"],first_slice=False)
    if not accepted:
        assert type(event["error"]) is str and event["error"]
        assert event["error_type"] in {"revision_conflict","document_unavailable","out_of_range","invalid_argument","exception"}
        if category is not None:
            assert event["error_type"] == category
        _same_collection(event["before"],event["after"])
    else:
        assert "error" not in event and "error_type" not in event
    return event


def _api_labels(row, extra):
    assert [item["label"] for item in row["api_observations"]] == ["fixture_preparation"]+list(extra)


def _extended_base(row):
    _physical(row)
    initial, _ = _state(row,"initial"); navigated, _ = _state(row,"navigated")
    source_id = _integer(row["source_document_id"],1)
    before, source = _owner(initial,source_id),_owner(navigated,source_id)
    _literal_source(before); _literal_source(source); _unchanged(before,source,display=False)
    assert before["display"]["t"] == before["display"]["c"] == 0
    assert source["display"]["t"] == 1 and source["display"]["c"] == 2
    assert initial["document_ids"] == navigated["document_ids"] == [source_id]
    assert initial["workflow"] == navigated["workflow"] == []
    _typed_text(row,"navigate.channel","navigation.channel","3")
    _typed_text(row,"navigate.time","navigation.time","2")
    setup = row["api_observations"][0]
    assert setup["label"] == "fixture_preparation" and setup["scope"] == "API fixture preparation only, not physical UI execution"
    _literal_source(setup["source"]); _unchanged(setup["source"],before)
    final, report = _state(row,"final")
    assert row["final_usage"] == ZERO_USAGE and row["window_error"] == ""
    assert final["busy"] is False and final["usage"] == ZERO_USAGE and final["results"] == [] and final["window_error"] == ""
    assert final["recording"] is True
    if row["case_id"] not in ("O10","O12"):
        for frame in row["frames"]:
            assert frame["native_state"]["busy"] is False and frame["native_state"]["usage"] == ZERO_USAGE
    return navigated,final,report


def _new_dialog(row, label, owner_id, *, t=1, c=2, profile="scientific_u16", rebind=False):
    state, report = _state(row,label); dialog = report["dialog"]
    shape,_ = SOURCE_PROFILES[profile]
    assert dialog["open"] is True and dialog["retained_source"] is True and dialog["is_operation"] is False
    assert dialog["opened_document_id"] == owner_id and state["active_document_id"] == owner_id
    assert dialog["opened_document_id_semantics"] == "captured_native_execution_source"
    assert _integer(dialog["captured_t"]) == t and _integer(dialog["captured_c"]) == c
    assert dialog["captured_channel_label"] == f"Channel {c+1} / {shape[1]}"
    assert dialog["captured_time_label"] == f"Time point {t+1} / {shape[0]}"
    assert dialog["is_extraction_rebind"] is rebind
    assert dialog["title"] == ("Rebind oriented range" if rebind else "Reslice oriented range")
    if not rebind:
        assert dialog["rebind_record"] is None
    return state,dialog,report


def _new_draft(row, label, request, owner_id, *, profile="scientific_u16", rebind=False):
    state,dialog,report = _new_dialog(row,label,owner_id,t=request["t"],c=request["c"],profile=profile,rebind=rebind)
    expected = {key:value for key,value in request.items() if key not in ("t","c","max_working_bytes")}
    expected["first"] += 1; expected["last"] += 1
    assert _typed(dialog["values"]) == _typed(expected)
    layout,selected,final = _layout_for(request,profile)
    base=AXES[request["plane"]]
    signs = (-1 if request["flip_vertical"] else 1,1,-1 if request["reverse_order"] else 1) if request["transpose"] else (1,-1 if request["flip_vertical"] else 1,-1 if request["reverse_order"] else 1)
    assert dialog["emitted_plane_count"] == len(selected) and dialog["last_selected_1based"] == max(selected)+1
    assert dialog["requested_last_1based"] == request["last"]+1
    assert dialog["effective_first_selected_1based"] == selected[0]+1 and dialog["effective_last_selected_1based"] == selected[-1]+1
    assert dialog["output_axis_order"] == ["XYZ"[axis] for axis in final] and dialog["output_axis_signs"] == list(signs)
    assert dialog["fixed_axis_label"] == f"{'XYZ'[base[2]]} indices: 1 to {(9,7,5)[base[2]]}"
    assert _integer(dialog["working_bytes"],1) == layout["working_bytes"] and dialog["can_apply"] is True
    assert dialog["controls"]["apply"]["enabled"] is True
    cap_mb = _integer(report["settings"]["processing_memory_mb"])
    assert dialog["max_working_bytes"] == (min(CAP,cap_mb*1024*1024) if cap_mb else CAP)
    return state


def _fresh_output(document, source, request, profile, *, operation="axis_range_orientation"):
    layout,selected,final = _literal_output_pixels(document,request,profile,operation=operation)
    assert _integer(document["document_id"],1) != source["document_id"] and document["data_identity"] == 0
    assert document["dirty"] is False and document["history"]["undo_steps"] == document["history"]["redo_steps"] == 0
    assert document["history"]["step_limit"] == source["history"]["step_limit"]
    assert document["history"]["pixel_budget"] == source["history"]["pixel_budget"]
    assert document["history"]["logical_bytes"] == document["history"]["current_bytes"] == math.prod(layout["shape"])*8
    assert all(document["revisions"][key] == 0 for key in ("pixels","metadata","selection"))
    assert document["display"]["t"] == document["display"]["c"] == 0
    assert document["selection"] is document["previous_selection"] is None
    assert document["rois"] == document["overlays"] == [] and document["spectrum_present"] is False
    assert document["metadata"]["properties"] == document["metadata"]["record_layout"] == {}
    base=AXES[request["plane"]]
    provenance = dict(schema_version=1,source_document_id=source["document_id"],plane=request["plane"],t=request["t"],c=request["c"],
        horizontal_axis="XYZ"[final[0]],vertical_axis="XYZ"[final[1]],fixed_axis="XYZ"[base[2]],index_convention="zero_based_voxel_centers")
    if operation == "axis_plane_export":
        provenance["coordinate"] = request["coordinate"]
        suffix = str(request["coordinate"]+1)
    else:
        provenance.update(first=request["first"],last=request["last"],step=request["step"],count=len(selected))
        suffix = f"{request['first']+1}-{request['last']+1} step {request['step']}"
        if operation == "axis_range_orientation":
            provenance.update(last_selected=max(selected),**{key:request[key] for key in ("reverse_order","flip_vertical","transpose")})
            suffix += f" [R{int(request['reverse_order'])} F{int(request['flip_vertical'])} T{int(request['transpose'])}]"
    assert document["metadata"]["title"] == source["metadata"]["title"]+" "+request["plane"]+" "+suffix
    assert _typed(document["metadata"]["extensions"]) == _typed({operation:provenance})
    input_addresses = {plane[key] for plane in source["planes"] for key in ("image_address","buffer_address")}
    assert not input_addresses.intersection({plane[key] for plane in document["planes"] for key in ("image_address","buffer_address")})


def _new_output(row, label, request, *, profile="scientific_u16", source_id=None, gesture="dialog.apply"):
    matches=[item for item in row["outputs"] if item["label"] == label]
    assert len(matches) == 1
    entry=matches[0]; source=entry["source_before"]; out=entry["output"]
    if source_id is None:
        source_id=row["source_document_id"]
    assert source["document_id"] == source_id
    assert entry["trigger"] == "physical_shared_imgui" and entry["api_label"] == ""
    assert _typed(entry["request"]) == _typed(request)
    _wire_record(entry["command_record"],source,request,profile)
    _fresh_output(out,source,request,profile); _same_data(source,entry["source_after"])
    before,after=entry["state_before"],entry["state_after"]
    _collection(before,first_slice=False); _collection(after,first_slice=False)
    assert before["busy"] is after["busy"] is False and before["recording"] is after["recording"] is True
    assert after["document_ids"] == before["document_ids"]+[out["document_id"]]
    assert before["active_document_id"] == source_id and after["active_document_id"] == out["document_id"]
    assert after["workflow"] == before["workflow"]+[entry["command_record"]]
    assert before["results"] == after["results"] == []
    for document in before["documents"]:
        _same_data(document,_owner(after,document["document_id"]))
    assert _typed(_owner(after,out["document_id"])) == _typed(out)
    observations=[item for item in row["publication_observations"] if item["label"] == label]
    assert [item["phase"] for item in observations] == ["before_native_publish","after_attach"]
    for observation in observations:
        ordinal=_integer(observation["frame_ordinal"],1,len(row["frames"]))
        frame=row["frames"][ordinal-1]
        assert frame["label"] in (gesture+".down",gesture+".up")
        assert frame["requested_events"][0]["type"] == "button"
    assert observations[0]["request"] == request and _typed(observations[0]["captured_source"]) == _typed(source)
    for key in ("document_id","shape","dtype","sample_policy","data_identity","metadata","selection","previous_selection","rois","overlays","planes","history"):
        assert _typed(observations[0]["candidate"][key]) == _typed(out[key])
    assert _typed(observations[1]["candidate"]) == _typed(out)
    return entry


def _output(row, entry, spec):
    assert entry["label"] == spec[0] and entry["trigger"] == "physical_shared_imgui"
    request = _request(spec)
    assert _typed(entry["request"]) == _typed(request)
    source, out = entry["source_before"], entry["output"]
    assert source["document_id"] == row["source_document_id"]
    _literal_source(source); _unchanged(source,entry["source_after"])
    assert source["display"]["t"] == 1 and source["display"]["c"] == 2
    expected, selected, final, _ = _derived(request)
    assert out["shape"] == expected["shape"] and out["dtype"] == "uint16" and out["sample_policy"] == "finite_only"
    _near(out["calibration"],expected["calibration"])
    assert out["document_id"] != source["document_id"] and out["data_identity"] == 0
    assert out["data_identity"] != source["data_identity"]
    source_addresses = {plane[key] for plane in source["planes"] for key in ("image_address", "buffer_address")}
    base = AXES[request["plane"]]
    for (ot, oc, k), plane in _planes(out).items():
        assert ot == oc == 0
        assert not source_addresses.intersection({plane["image_address"],plane["buffer_address"]})
        values = []
        for v in range(out["shape"][3]):
            for u in range(out["shape"][4]):
                horizontal, vertical = (v,u) if request["transpose"] else (u,v)
                xyz = [0,0,0]
                xyz[base[0]] = horizontal
                xyz[base[1]] = (9,7,5)[base[1]]-1-vertical if request["flip_vertical"] else vertical
                xyz[base[2]] = selected[k]
                values.append(40000 + (97 + 31*2 + 11*xyz[2] + 5*xyz[1] + 3*xyz[0]) % 251)
                _near(_world(out["calibration"],[u,v,k]),_world(CALIBRATION,xyz))
        assert plane["declared_hex"] == struct.pack("<"+"H"*len(values),*values).hex()
        assert plane["storage_f64le_hex"] == struct.pack("<"+"d"*len(values),*values).hex()
    assert out["dirty"] is False and out["history"]["undo_steps"] == out["history"]["redo_steps"] == 0
    assert out["history"]["logical_bytes"] == out["history"]["current_bytes"] == math.prod(out["shape"])*8
    assert out["history"]["step_limit"] == source["history"]["step_limit"]
    assert out["history"]["pixel_budget"] == source["history"]["pixel_budget"]
    assert all(out["revisions"][key] == 0 for key in ("pixels", "metadata", "selection"))
    assert out["display"]["t"] == out["display"]["c"] == 0
    for value, extent in zip(out["display"]["crosshair"], (out["shape"][4],out["shape"][3],out["shape"][2])):
        _integer(value, 0, extent-1)
    assert out["selection"] is None and out["previous_selection"] is None
    assert out["rois"] == out["overlays"] == [] and out["spectrum_present"] is False
    reverse, flip, transpose = (int(request[key]) for key in ("reverse_order", "flip_vertical", "transpose"))
    assert out["metadata"]["title"] == (f"Scientific orientation source {request['plane']} {request['first']+1}-{request['last']+1} "
        f"step {request['step']} [R{reverse} F{flip} T{transpose}]")
    assert out["metadata"]["properties"] == out["metadata"]["record_layout"] == {}
    provenance = dict(schema_version=1,source_document_id=source["document_id"],plane=request["plane"],
        first=request["first"],last=request["last"],step=request["step"],last_selected=max(selected),count=len(selected),t=1,c=2,
        reverse_order=request["reverse_order"],flip_vertical=request["flip_vertical"],transpose=request["transpose"],
        horizontal_axis="XYZ"[final[0]],vertical_axis="XYZ"[final[1]],fixed_axis="XYZ"[base[2]],
        index_convention="zero_based_voxel_centers")
    assert _typed(out["metadata"]["extensions"]) == _typed({"axis_range_orientation":provenance})
    _record(entry["command_record"],source,request)
    before, after = entry["state_before"], entry["state_after"]
    _collection(before); _collection(after)
    assert after["document_ids"] == before["document_ids"]+[out["document_id"]]
    assert before["active_document_id"] == source["document_id"] and after["active_document_id"] == out["document_id"]
    assert after["workflow"] == before["workflow"]+[entry["command_record"]]
    assert after["results"] == before["results"] == []
    for old in before["documents"]:
        _unchanged(old,_owner(after,old["document_id"]))
    assert _typed(_owner(after,out["document_id"])) == _typed(out)
    observations = [item for item in row["publication_observations"] if item["label"] == spec[0]]
    assert [item["phase"] for item in observations] == ["before_native_publish","after_attach"]
    for observation in observations:
        frame = row["frames"][_integer(observation["frame_ordinal"],1,len(row["frames"]))-1]
        assert frame["label"] in {"dialog.apply.down","dialog.apply.up"}
        assert frame["requested_events"][0]["type"] == "button"
    staged = observations[0]
    assert staged["request"] == request and _typed(staged["captured_source"]) == _typed(source)
    _planes(staged["candidate"])
    for key in ("shape", "dtype", "sample_policy", "data_identity", "metadata", "selection", "previous_selection", "rois", "overlays", "planes", "history"):
        assert _typed(staged["candidate"][key]) == _typed(out[key])
    assert _typed(observations[1]["candidate"]) == _typed(out)


def _frames_label(row, label):
    matches = [item for item in row["frames"] if item["label"] == label]
    assert len(matches) == 1, "Missing/duplicate actual input frame: " + label
    return matches[0]


def _typed_text(row, label, selector, text):
    frame = _frames_label(row,label+".text")
    assert frame["requested_events"] == [{"type":"text","value":text}]
    actual = frame["report"]["input_frame"]
    control = _control(frame["report"],selector)
    assert actual["input_text"]["id"] == actual["focus"]["active_id"] == control["id"] != 0
    for down, suffix in ((True,".select_all.down"),(False,".select_all.up")):
        event = _frames_label(row,label+suffix)
        assert event["ordinal"] < frame["ordinal"]
        assert event["requested_events"] == [{"type":"keys","keys":[{"name":"Ctrl","down":down},{"name":"A","down":down}]}]


def _clicked(row, selector, start=0, end=None, *, disabled=False):
    end = len(row["frames"]) if end is None else end
    matches = [item for item in row["control_observations"] if item["selector"] == selector and
               start < item["before_frame_ordinal"]+3 <= end and item["disabled_attempt"] is disabled]
    assert matches, "Missing actual eligible control gesture: " + selector
    return matches


def _dialog(row, label):
    state, report = _state(row,label)
    dialog = report["dialog"]
    assert dialog["open"] is True and dialog["retained_source"] is True
    assert dialog["title"] == "Reslice oriented range" and dialog["is_operation"] is False
    assert dialog["opened_document_id"] == row["source_document_id"]
    assert dialog["opened_document_id_semantics"] == "captured_native_execution_source"
    assert type(dialog["captured_t"]) is int and dialog["captured_t"] == 1
    assert type(dialog["captured_c"]) is int and dialog["captured_c"] == 2
    assert dialog["captured_channel_label"] == "Channel 3 / 3" and dialog["captured_time_label"] == "Time point 2 / 2"
    assert dialog["is_extraction_rebind"] is False and dialog["rebind_record"] is None
    assert state["active_document_id"] == row["source_document_id"]
    return state, dialog, report


def _defaults(row, label, start=0):
    state, dialog, report = _dialog(row,label)
    expected = dict(plane="XZ",first=1,last=7,step=1,reverse_order=False,flip_vertical=False,transpose=False)
    assert _typed(dialog["values"]) == _typed(expected)
    assert dialog["fixed_axis_label"] == "Y indices: 1 to 7" and dialog["emitted_plane_count"] == 7
    assert dialog["can_apply"] is True and dialog["controls"]["apply"]["enabled"] is True
    assert dialog["controls"]["submit"] == dialog["controls"]["apply"]
    descriptor, _, _, _ = _derived(dict(plane="XZ",first=0,last=6,step=1,t=1,c=2,max_working_bytes=CAP,
                                      reverse_order=False,flip_vertical=False,transpose=False))
    assert _integer(dialog["working_bytes"],1) == descriptor["working_bytes"]
    memory_mb = _integer(report["settings"]["processing_memory_mb"])
    expected_cap = min(CAP,memory_mb*1024*1024) if memory_mb else CAP
    assert _integer(dialog["max_working_bytes"],1) == expected_cap
    _clicked(row,"menu.Image",start,end=state["frame_ordinal"])
    _clicked(row,"menu.Image.Stacks",start,end=state["frame_ordinal"])
    _clicked(row,"menu.action.show_axis_range_orientation",start,end=state["frame_ordinal"])
    return state


def _draft(row, spec):
    label = spec[0]
    state, dialog, report = _dialog(row,label+".draft")
    request = _request(spec)
    expected_values = {key:value for key,value in request.items() if key not in ("t","c","max_working_bytes")}
    expected_values["first"] += 1; expected_values["last"] += 1
    assert _typed(dialog["values"]) == _typed(expected_values)
    expected, selected, final, signs = _derived(request)
    base = AXES[request["plane"]]
    assert dialog["fixed_axis_label"] == f"{'XYZ'[base[2]]} indices: 1 to {(9,7,5)[base[2]]}"
    assert dialog["emitted_plane_count"] == len(selected)
    assert dialog["last_selected_1based"] == max(selected)+1
    assert dialog["requested_last_1based"] == request["last"]+1
    assert dialog["effective_first_selected_1based"] == selected[0]+1 and dialog["effective_last_selected_1based"] == selected[-1]+1
    assert dialog["output_axis_order"] == ["XYZ"[axis] for axis in final]
    assert dialog["output_axis_signs"] == list(signs) and dialog["flip_before_transpose"] is True
    assert _integer(dialog["working_bytes"],1) == expected["working_bytes"]
    memory_mb = _integer(report["settings"]["processing_memory_mb"])
    expected_cap = min(CAP,memory_mb*1024*1024) if memory_mb else CAP
    assert _integer(dialog["max_working_bytes"],1) == expected_cap
    assert dialog["can_apply"] is True and dialog["controls"]["apply"]["enabled"] is True
    return state


def _common(row):
    _physical(row)
    assert row["fixture_recipe"]["shape_tczyxs"] == SHAPE and row["fixture_recipe"]["dtype"] == "uint16"
    _near(row["fixture_recipe"]["calibration"],CALIBRATION)
    assert type(row["source_document_id"]) is int and row["source_document_id"] > 0
    initial, _ = _state(row,"initial")
    navigated, _ = _state(row,"navigated")
    source = _owner(initial,row["source_document_id"])
    _literal_source(source)
    after_nav = _owner(navigated,row["source_document_id"])
    _literal_source(after_nav); _unchanged(source,after_nav,display=False)
    assert source["display"]["t"] == source["display"]["c"] == 0
    assert after_nav["display"]["t"] == 1 and after_nav["display"]["c"] == 2
    _typed_text(row,"navigate.channel","navigation.channel","3")
    _typed_text(row,"navigate.time","navigation.time","2")
    assert initial["workflow"] == navigated["workflow"] == []
    assert initial["document_ids"] == navigated["document_ids"] == [row["source_document_id"]]
    final, _ = _state(row,"final")
    _literal_source(_owner(final,row["source_document_id"]))
    _unchanged(after_nav,_owner(final,row["source_document_id"]))
    assert row["final_usage"] == ZERO_USAGE and row["window_error"] == ""
    assert len(row["api_observations"]) == 1
    setup = row["api_observations"][0]
    assert setup["label"] == "fixture_preparation" and setup["scope"] == "API fixture preparation only, not physical UI execution"
    _literal_source(setup["source"])
    _unchanged(setup["source"],source)
    return navigated, final


def _positive_case(row):
    navigated, final = _common(row)
    specs = EXPECTED_OUTPUTS[row["case_id"]]
    assert [item["label"] for item in row["outputs"]] == [spec[0] for spec in specs]
    assert len(row["publication_observations"]) == 2*len(specs)
    records, output_ids = [], []
    last_ordinal = navigated["frame_ordinal"]
    for entry, spec in zip(row["outputs"],specs):
        label = spec[0]
        _defaults(row,label+".opened",last_ordinal)
        for name, text in (("first","2"),("last",str(spec[3]+1)),("step","2")):
            _typed_text(row,label+"."+name,"dialog."+name,text)
        draft = _draft(row,spec)
        assert draft["frame_ordinal"] > last_ordinal
        for name, enabled in zip(("reverse_order","flip_vertical","transpose"),spec[5:]):
            gestures = [item for item in row["control_observations"] if item["selector"] == "dialog."+name and
                        last_ordinal < item["before_frame_ordinal"]+3 < draft["frame_ordinal"]]
            assert len(gestures) == int(enabled)
        _output(row,entry,spec)
        published, report = _state(row,label+".published")
        _clicked(row,"dialog.apply",draft["frame_ordinal"],published["frame_ordinal"])
        assert report["dialog"]["open"] is False
        assert _typed(published["documents"]) == _typed(entry["state_after"]["documents"])
        records.append(entry["command_record"]); output_ids.append(entry["output"]["document_id"])
        assert published["workflow"] == records
        last_ordinal = published["frame_ordinal"]
    assert final["workflow"] == records and final["document_ids"] == [row["source_document_id"]]+output_ids
    assert final["active_document_id"] == output_ids[-1]
    if len(specs) == 2:
        _clicked(row,"document."+str(row["source_document_id"]),row["states"][specs[0][0]+".published"]["frame_ordinal"],
                 row["states"][specs[1][0]+".opened"]["frame_ordinal"])


def _cancel_case(row):
    navigated, final = _common(row)
    _defaults(row,"defaults.opened",navigated["frame_ordinal"])
    before, _, _ = _dialog(row,"before_cancel")
    after, report = _state(row,"after_cancel")
    _clicked(row,"dialog.cancel",before["frame_ordinal"],after["frame_ordinal"])
    assert report["dialog"]["open"] is False
    for state in (before,after,final):
        for key in ("document_ids","facade_ids","active_document_id","busy","recording","workflow","results","usage","window_error"):
            assert _typed(state[key]) == _typed(navigated[key])
        _unchanged(_owner(navigated,row["source_document_id"]),_owner(state,row["source_document_id"]))
    assert row["outputs"] == row["publication_observations"] == []


def _invalid_case(row):
    navigated, final = _common(row)
    _defaults(row,"invalid.opened",navigated["frame_ordinal"])
    for label, selector, text in (("invalid.first","dialog.first","-"),
                                  ("invalid.first.corrected","dialog.first","2"),
                                  ("invalid.last","dialog.last","-"),
                                  ("invalid.last.corrected","dialog.last","9"),
                                  ("invalid.step.zero","dialog.step","0"),
                                  ("invalid.step.partial","dialog.step","-"),
                                  ("corrected.last","dialog.last","7"),
                                  ("corrected.step","dialog.step","2")):
        _typed_text(row,label,selector,text)
    for label, field, text in (("invalid.first","first","-"),("invalid.last","last","-"),("invalid.step.zero","step","0")):
        before, dialog, _ = _dialog(row,label+".before_disabled")
        after, report = _state(row,label+".after_disabled")
        assert dialog["text"][field] == text and dialog["can_apply"] is False
        assert dialog["controls"]["apply"]["enabled"] is False
        assert type(dialog["error"]) is str and dialog["error"]
        _clicked(row,"dialog.apply",before["frame_ordinal"],after["frame_ordinal"],disabled=True)
        assert report["dialog"]["open"] is True and report["dialog"]["can_apply"] is False
        assert report["dialog"]["text"][field] == text
        assert before["document_ids"] == after["document_ids"] == [row["source_document_id"]]
        assert before["workflow"] == after["workflow"] == []
        _unchanged(_owner(before,row["source_document_id"]),_owner(after,row["source_document_id"]))
    for label, plane, field, text in (("invalid.first.xy","XY","first","-"),
                                     ("invalid.flags.xy","XY","first","-"),
                                     ("invalid.last.yz","YZ","last","-"),
                                     ("invalid.step.xz","XZ","step","-")):
        state, dialog, _ = _dialog(row,label)
        assert dialog["values"]["plane"] == plane and dialog["text"][field] == text
        assert dialog["can_apply"] is False and dialog["controls"]["apply"]["enabled"] is False
        assert state["document_ids"] == [row["source_document_id"]] and state["workflow"] == []
        _unchanged(_owner(navigated,row["source_document_id"]),_owner(state,row["source_document_id"]))
    for label, plane, start_label in (("invalid.first.xy","XY","invalid.first.after_disabled"),
                                      ("invalid.last.yz","YZ","invalid.last.after_disabled"),
                                      ("invalid.step.xz","XZ","invalid.step.zero.after_disabled")):
        _clicked(row,"dialog.plane.choice."+plane,row["states"][start_label]["frame_ordinal"],row["states"][label]["frame_ordinal"])
    flag_state, flag_dialog, _ = _dialog(row,"invalid.flags.xy")
    assert all(flag_dialog["values"][key] is True for key in ("reverse_order","flip_vertical","transpose"))
    for flag in ("reverse_order","flip_vertical","transpose"):
        _clicked(row,"dialog."+flag,end=flag_state["frame_ordinal"])
    spec, = EXPECTED_OUTPUTS["O06"]
    assert len(row["outputs"]) == 1
    draft = _draft(row,spec); _output(row,row["outputs"][0],spec)
    published, report = _state(row,spec[0]+".published")
    _clicked(row,"dialog.apply",draft["frame_ordinal"],published["frame_ordinal"])
    assert report["dialog"]["open"] is False
    assert final["workflow"] == [row["outputs"][0]["command_record"]]
    assert final["document_ids"] == [row["source_document_id"],row["outputs"][0]["output"]["document_id"]]
    assert final["active_document_id"] == row["outputs"][0]["output"]["document_id"]
    assert len(row["publication_observations"]) == 2


def _edited_property(event, owner_id):
    assert event["inputs"] == dict(method="DocumentTransaction.set_property",key="owned_stale",value=True)
    before,after=_owner(event["before"],owner_id),_owner(event["after"],owner_id)
    _profile_pixels(before,"scientific_u16"); _profile_pixels(after,"scientific_u16")
    for key in ("shape","dtype","sample_policy","calibration","selection","previous_selection","rois","overlays","spectrum_present","planes","display"):
        assert _typed(before[key]) == _typed(after[key])
    assert after["metadata"] == dict(before["metadata"],properties=dict(before["metadata"]["properties"],owned_stale=True))
    assert after["revisions"]["metadata"] == before["revisions"]["metadata"]+1
    for key in ("pixels","selection","display"):
        assert after["revisions"][key] == before["revisions"][key]
    assert after["history"]["undo_steps"] == before["history"]["undo_steps"]+1 and after["history"]["redo_steps"] == 0
    for key in set(before["history"])-{"undo_steps","redo_steps"}:
        assert _typed(before["history"][key]) == _typed(after["history"][key])
    assert after["dirty"] is True and after["data_identity"] != before["data_identity"]
    for key in ("document_ids","facade_ids","active_document_id","busy","recording","workflow","results","usage","window_error"):
        assert _typed(event["before"][key]) == _typed(event["after"][key])
    return before,after


def _added_fixture(row, label, profile):
    event=_api(row,label+".fixture")
    owner=event["owner"]
    _profile_pixels(owner,profile)
    assert event["facade_scientific"] is (profile != "eager_u16")
    assert event["inputs"]["method"] == "Window.addNativeDocument"
    assert event["inputs"]["dtype"] == SOURCE_PROFILES[profile][1]
    assert event["inputs"]["samples"] == SOURCE_PROFILES[profile][0][5]
    assert event["inputs"]["eager"] is (profile == "eager_u16")
    assert owner["metadata"]["title"] == "Scientific orientation "+label
    assert owner["history"]["undo_steps"] == owner["history"]["redo_steps"] == 0 and owner["dirty"] is False
    assert event["after"]["document_ids"] == event["before"]["document_ids"]+[owner["document_id"]]
    assert event["after"]["active_document_id"] == owner["document_id"]
    assert _typed(_owner(event["after"],owner["document_id"])) == _typed(owner)
    for old in event["before"]["documents"]:
        _same_data(old,_owner(event["after"],old["document_id"]))
    for key in ("workflow","results","busy","recording","usage","window_error"):
        assert _typed(event["before"][key]) == _typed(event["after"][key])
    if profile == "eager_u16":
        assert all(roi["t"] is None and roi["c"] is None for roi in owner["rois"])
        assert [roi["z"] for roi in owner["rois"]] == [0,1]
    return owner


def _capture_lifetime_case(row):
    navigated,final,_=_extended_base(row); source_id=row["source_document_id"]
    _api_labels(row,("captured.display_navigation","other.fixture","inactive.orientation_refusal",
                     "stale.metadata_fixture","stale.strict_refusal","closed.orientation_refusal"))
    assert row["outputs"] == row["publication_observations"] == [] and final["workflow"] == []
    default_request=_new_request("XZ",0,6,1)
    _wire_record(row["original_record"],_owner(navigated,source_id),default_request,"scientific_u16")
    _new_dialog(row,"captured.opened",source_id)
    event=_api(row,"captured.display_navigation")
    assert event["inputs"] == dict(method="Window.setNativeDisplay",t=0,c=0,zoom=2.5)
    before,after=_owner(event["before"],source_id),_owner(event["after"],source_id)
    _same_data(before,after,display=False)
    assert (after["display"]["t"],after["display"]["c"],after["display"]["crosshair"],after["display"]["zoom"]) == (0,0,[7,5,4],2.5)
    _,dialog,_=_new_dialog(row,"captured.after_navigation",source_id)
    assert dialog["values"]["plane"] == "XZ" and dialog["values"]["first"] == 1 and dialog["values"]["last"] == 7
    cancelled,report=_state(row,"captured.cancelled")
    _clicked(row,"dialog.cancel",row["states"]["captured.after_navigation"]["frame_ordinal"],cancelled["frame_ordinal"])
    assert report["dialog"]["open"] is False
    _same_data(after,_owner(cancelled,source_id))
    other=_added_fixture(row,"other","scientific_u16")
    refused=_api(row,"inactive.orientation_refusal",accepted=False,category="invalid_argument")
    assert refused["inputs"]["captured_source_id"] == source_id and refused["inputs"]["request"] == _new_request()
    assert refused["before"]["active_document_id"] == other["document_id"]
    returned,_=_state(row,"owner.returned")
    assert returned["active_document_id"] == source_id
    _clicked(row,"document."+str(source_id),cancelled["frame_ordinal"],returned["frame_ordinal"])
    _,edited=_edited_property(_api(row,"stale.metadata_fixture"),source_id)
    stale=_api(row,"stale.strict_refusal",accepted=False,category="revision_conflict")
    assert stale["inputs"]["record"] == row["original_record"]
    closed,report=_state(row,"owner.closed")
    before_close,_=_state(row,"owner.before_close")
    _clicked(row,"menu.File.Close_image",before_close["frame_ordinal"],closed["frame_ordinal"])
    _clicked(row,"close_prompt.discard",before_close["frame_ordinal"],closed["frame_ordinal"])
    assert source_id not in closed["document_ids"] and closed["document_ids"] == [other["document_id"]]
    _same_data(edited,row["closed_source"])
    refusal=_api(row,"closed.orientation_refusal",accepted=False)
    assert refusal["inputs"]["captured_source_id"] == source_id and source_id not in refusal["before"]["document_ids"]
    recovery,_,_=_new_dialog(row,"recovery.opened",other["document_id"],t=0,c=0)
    end,report=_state(row,"recovery.cancelled")
    _clicked(row,"dialog.cancel",recovery["frame_ordinal"],end["frame_ordinal"])
    assert report["dialog"]["open"] is False
    assert final["document_ids"] == [other["document_id"]] and final["active_document_id"] == other["document_id"]
    _same_data(other,_owner(final,other["document_id"]))


def _workflow_replay_case(row):
    navigated,final,_=_extended_base(row); source_id=row["source_document_id"]
    _api_labels(row,("replay.display_navigation","replay.stale_metadata_fixture","replay.stale_refusal"))
    request=_new_request()
    assert [item["label"] for item in row["outputs"]] == ["original.xy.000","replay.strict"]
    first=_new_output(row,"original.xy.000",request)
    _new_draft(row,"original.xy.000.draft",request,source_id)
    _clicked(row,"dialog.apply",row["states"]["original.xy.000.draft"]["frame_ordinal"],row["states"]["original.xy.000.published"]["frame_ordinal"])
    assert row["original_records"] == [first["command_record"]]
    nav=_api(row,"replay.display_navigation")
    assert nav["inputs"] == dict(method="Window.setNativeDisplay",t=0,c=0,zoom=2.5)
    _same_data(_owner(nav["before"],source_id),_owner(nav["after"],source_id),display=False)
    assert _owner(nav["after"],source_id)["display"]["t"] == _owner(nav["after"],source_id)["display"]["c"] == 0
    assert _owner(nav["after"],source_id)["display"]["zoom"] == 2.5
    replayed=_new_output(row,"replay.strict",request,gesture="workflow.orientation.0.replay")
    assert replayed["command_record"] == first["command_record"]
    assert replayed["source_before"]["display"]["t"] == replayed["source_before"]["display"]["c"] == 0
    _clicked(row,"workflow.orientation.0.replay",row["states"]["replay.navigated"]["frame_ordinal"],row["states"]["replay.strict.published"]["frame_ordinal"])
    _,edited=_edited_property(_api(row,"replay.stale_metadata_fixture"),source_id)
    refusal=_api(row,"replay.stale_refusal",accepted=False,category="revision_conflict")
    assert refusal["inputs"]["record"] == first["command_record"]
    assert final["workflow"] == [first["command_record"],first["command_record"]]
    assert final["document_ids"] == [source_id,first["output"]["document_id"],replayed["output"]["document_id"]]
    assert final["active_document_id"] == source_id
    _same_data(edited,_owner(final,source_id))
    assert len(row["publication_observations"]) == 4


def _case_file(row, label, member):
    observed=row["files"][label]
    assert observed["member"] == member and Path(member).name == member and "/" not in member and "\\" not in member
    assert observed["scope"] == "API nativeV3 I/O; no physical file-dialog credit"
    size=_integer(observed["bytes"],1,MAX_ARCHIVE)
    raw=bytes.fromhex(observed["hex"])
    assert len(raw) == size and hashlib.sha256(raw).hexdigest() == observed["sha256"]
    _,provenance=_inputs()
    descriptor=next(item for item in provenance["cases"] if (item["id"],item["context"]) == (row["case_id"],row["context"]))
    path=_pin(descriptor["raw"]).parent/member
    _pin(dict(path=str(path),bytes=size,sha256=observed["sha256"]),MAX_ARCHIVE)
    assert path.read_bytes() == raw
    assert descriptor["raw"]["bytes"]+descriptor["case_index"]["bytes"]+sum(item["bytes"] for item in row["files"].values()) <= MAX_ARCHIVE
    assert raw.startswith(b"IMAGEJXX3\n") and len(raw) >= 18
    declared,=struct.unpack("<Q",raw[10:18])
    assert 0 < declared <= MAX_ARCHIVE
    stream=zlib.decompressobj()
    plain=stream.decompress(raw[18:],declared+1)
    assert len(plain) == declared and stream.eof and not stream.unused_data and not stream.unconsumed_tail
    session=json.loads(plain,object_pairs_hook=_unique,parse_constant=_nonfinite)
    assert set(session) == {"format","version","documents","results","workflow","extensions"}
    assert session["format"] == "imagejxx-session" and type(session["version"]) is int and session["version"] == 3
    return session


def _workflow_rebind_case(row):
    _,final,_=_extended_base(row); source_id=row["source_document_id"]
    _api_labels(row,("rebind.save_native_v3","rebind.open_native_v3","rebind.fresh_owner_strict_refusal"))
    first_request=_new_request(); first=_new_output(row,"original.xy.000",first_request)
    assert row["original_records"] == [first["command_record"]]
    saved=_api(row,"rebind.save_native_v3")
    assert saved["inputs"] == dict(method="Window.action.save_document",member="rebind-source.ijx")
    assert set(row["files"]) == {"rebind_source"}
    session=_case_file(row,"rebind_source","rebind-source.ijx")
    assert session["workflow"] == row["original_records"] and session["results"] == []
    assert len(session["documents"]) == 1
    saved_document=session["documents"][0]
    source_at_save=_owner(saved["after"],source_id)
    assert saved_document["axes"] == "TCZYXS" and saved_document["shape"] == SHAPE
    assert saved_document["pixel_type"] == "uint16" and saved_document["sample_policy"] == "finite_only"
    assert saved_document["storage"] == "Float64LE"
    assert type(saved_document["planes"]) is list and len(saved_document["planes"]) == math.prod(SHAPE[:3])
    plane_bytes=math.prod(SHAPE[3:])*8
    saved_planes=[]
    for encoded_plane in saved_document["planes"]:
        assert type(encoded_plane) is str and len(encoded_plane) == 4*((plane_bytes+2)//3)
        decoded_plane=base64.b64decode(encoded_plane,validate=True)
        assert len(decoded_plane) == plane_bytes and base64.b64encode(decoded_plane).decode("ascii") == encoded_plane
        saved_planes.append(decoded_plane.hex())
    assert saved_planes == [plane["storage_f64le_hex"] for plane in source_at_save["planes"]]
    _near(saved_document["calibration"],CALIBRATION)
    opened=_api(row,"rebind.open_native_v3")
    assert opened["inputs"] == dict(method="Window.openFile",member="rebind-source.ijx")
    fresh_id=_integer(row["reopened_document_id"],1)
    assert fresh_id not in opened["before"]["document_ids"] and opened["after"]["active_document_id"] == fresh_id
    assert opened["after"]["document_ids"] == opened["before"]["document_ids"]+[fresh_id]
    fresh=opened["owner"]
    assert fresh["document_id"] == fresh_id and _typed(fresh) == _typed(_owner(opened["after"],fresh_id))
    _profile_pixels(fresh,"scientific_u16")
    original=_owner(saved["after"],source_id)
    for key in ("metadata","selection","previous_selection","rois","overlays","spectrum_present","calibration","shape","dtype","sample_policy"):
        assert _typed(fresh[key]) == _typed(original[key])
    assert fresh["dirty"] is False and fresh["history"]["undo_steps"] == fresh["history"]["redo_steps"] == 0
    assert fresh["data_identity"] == 0
    _typed_text(row,"rebind.destination.channel","navigation.channel","2")
    _typed_text(row,"rebind.destination.time","navigation.time","1")
    destination,_=_state(row,"rebind.destination")
    assert (_owner(destination,fresh_id)["display"]["t"],_owner(destination,fresh_id)["display"]["c"]) == (0,1)
    refused=_api(row,"rebind.fresh_owner_strict_refusal",accepted=False,category="invalid_argument")
    assert refused["inputs"]["record"] == first["command_record"] and refused["inputs"]["document_id"] == fresh_id
    form,dialog,_=_new_dialog(row,"rebind.opened",fresh_id,t=0,c=1,rebind=True)
    assert dialog["rebind_record"] == first["command_record"]
    assert dialog["values"] == dict(plane="XY",first=2,last=5,step=2,reverse_order=False,flip_vertical=False,transpose=False)
    _clicked(row,"workflow.orientation.0.rebind",destination["frame_ordinal"],form["frame_ordinal"])
    request=_new_request("YZ",1,8,2,t=0,c=1,flags=(True,True,True))
    draft=_new_draft(row,"rebind.yz.111.draft",request,fresh_id,rebind=True)
    for field,text in (("first","2"),("last","9"),("step","2")):
        _typed_text(row,"rebind."+field,"dialog."+field,text)
    for flag in ("reverse_order","flip_vertical","transpose"):
        _clicked(row,"dialog."+flag,form["frame_ordinal"],draft["frame_ordinal"])
    rebound=_new_output(row,"rebind.yz.111",request,source_id=fresh_id)
    _clicked(row,"dialog.apply",draft["frame_ordinal"],row["states"]["rebind.yz.111.published"]["frame_ordinal"])
    assert row["original_record_after_rebind"] == first["command_record"] == final["workflow"][0]
    assert final["workflow"] == [first["command_record"],rebound["command_record"]]
    assert [item["label"] for item in row["outputs"]] == ["original.xy.000","rebind.yz.111"]
    assert final["active_document_id"] == rebound["output"]["document_id"] and len(row["publication_observations"]) == 4


def _job(value, source_id, state):
    assert set(value) == {"id","document_id","label","state_code","completed","total","checkpoints","error",
                          "cancel_requested","resource_bytes","peak_reserved_bytes","reservation_acquired"}
    _integer(value["id"],1)
    assert value["document_id"] == source_id and type(value["label"]) is str and value["label"]
    assert _integer(value["state_code"],0,6) == state
    assert 0 <= _integer(value["completed"]) <= _integer(value["total"],1)
    _integer(value["checkpoints"],1)
    assert value["error"] == ""
    _boolean(value["cancel_requested"]); _boolean(value["reservation_acquired"])
    assert _integer(value["resource_bytes"],1) <= _integer(value["peak_reserved_bytes"],1)
    assert value["reservation_acquired"] is True


def _preview_case(row):
    navigated,final,_=_extended_base(row); source_id=row["source_document_id"]
    _api_labels(row,())
    assert row["files"] == {} and row["legacy_outputs"] == row["failed_publications"] == []
    ready,ready_report=_state(row,"preview.ready")
    _clicked(row,"menu.Process",navigated["frame_ordinal"],ready["frame_ordinal"])
    _clicked(row,"menu.Process.Filters",navigated["frame_ordinal"],ready["frame_ordinal"])
    _clicked(row,"menu.command.gaussian_blur_imagej",navigated["frame_ordinal"],ready["frame_ordinal"])
    _clicked(row,"gaussian.preview",navigated["frame_ordinal"],ready["frame_ordinal"])
    assert [item["label"] for item in row["preview_observations"]] == ["ready","after_cancel","after_invalid"]
    first=row["preview_observations"][0]
    initial_preview=first["preview"]
    # This consumer tests committed-source isolation, not a second Gaussian
    # numerical oracle. A genuine installed READY preview must differ from
    # its literal committed source, while both byte readbacks remain present.
    preview_planes=_planes(initial_preview)
    source_planes=_planes(first["committed"])
    changed={position for position in source_planes if
             preview_planes[position]["declared_hex"] != source_planes[position]["declared_hex"] or
             preview_planes[position]["storage_f64le_hex"] != source_planes[position]["storage_f64le_hex"]}
    assert changed == {(1,2,2)}
    generation=_integer(first["gaussian_jobs"]["active_installed_generation"],1)
    for item in row["preview_observations"]:
        _integer(item["frame_ordinal"],1,len(row["frames"]))
        _job(item["job"],source_id,2)
        assert item["job"]["id"] == first["job"]["id"] and item["job"]["cancel_requested"] is False
        assert item["job"]["completed"] == item["job"]["total"]
        assert item["gaussian_jobs"]["active_installed_generation"] == generation
        assert item["gaussian_jobs"]["preview"]["owns_job"] is True
        assert item["gaussian_jobs"]["apply_callback_count"] == 0
        assert _integer(item["gaussian_jobs"]["resource_usage"]["retained_reserved_bytes"],1) > 0
        _literal_source(item["committed"])
        _same_data(_owner(navigated,source_id),item["committed"])
        for key in ("shape","dtype","sample_policy","calibration","planes"):
            assert _typed(item["preview"][key]) == _typed(initial_preview[key])
    _new_dialog(row,"preview.cancel.opened",source_id)
    cancelled,cancel_report=_state(row,"preview.cancelled")
    _clicked(row,"dialog.cancel",row["states"]["preview.cancel.opened"]["frame_ordinal"],cancelled["frame_ordinal"])
    assert cancel_report["dialog"]["open"] is False
    invalid,dialog,_=_new_dialog(row,"preview.invalid.before_disabled",source_id)
    assert dialog["text"]["step"] == "-" and dialog["can_apply"] is False
    assert dialog["controls"]["apply"]["enabled"] is False and dialog["error"]
    after,after_report=_state(row,"preview.invalid.after_disabled")
    _clicked(row,"dialog.apply",invalid["frame_ordinal"],after["frame_ordinal"],disabled=True)
    assert after_report["dialog"]["text"]["step"] == "-" and after_report["dialog"]["open"] is True
    _same_collection(invalid,after)
    _typed_text(row,"preview.invalid.step","dialog.step","-")
    for field,text in (("first","2"),("last","5"),("step","2")):
        _typed_text(row,"preview."+field,"dialog."+field,text)
    request=_new_request()
    draft=_new_draft(row,"preview.committed.xy.000.draft",request,source_id)
    output=_new_output(row,"preview.committed.xy.000",request)
    _clicked(row,"dialog.apply",draft["frame_ordinal"],row["states"]["preview.committed.xy.000.published"]["frame_ordinal"])
    assert len(row["outputs"]) == 1 and len(row["publication_observations"]) == 2
    assert final["workflow"] == [output["command_record"]]
    assert final["document_ids"] == [source_id,output["output"]["document_id"]]
    assert final["active_document_id"] == output["output"]["document_id"]
    _same_data(_owner(navigated,source_id),_owner(final,source_id))
    active,report=_state(row,"preview.output_active")
    assert active["usage"] == ZERO_USAGE and report["gaussian_jobs"]["active_installed_generation"] is None
    assert report["gaussian_jobs"]["preview"]["owns_job"] is False


def _legacy_output(row, label, request, operation):
    matches=[item for item in row["legacy_outputs"] if item["label"] == label]
    assert len(matches) == 1
    entry=matches[0]
    assert entry["scope"] == "Physical shared-ImGui V1 default export"
    source,out=entry["source_before"],entry["output"]
    assert source["document_id"] == row["source_document_id"]
    _wire_record(entry["record"],source,request,"scientific_u16",operation=operation)
    _fresh_output(out,source,request,"scientific_u16",operation=operation)
    _same_data(source,entry["source_after"])
    before,after=entry["before"],entry["after"]
    _collection(before,first_slice=False); _collection(after,first_slice=False)
    assert after["document_ids"] == before["document_ids"]+[out["document_id"]]
    assert before["active_document_id"] == source["document_id"] and after["active_document_id"] == out["document_id"]
    assert after["workflow"] == before["workflow"]+[entry["record"]]
    assert after["results"] == before["results"] == [] and after["usage"] == before["usage"] == ZERO_USAGE
    for old in before["documents"]:
        _same_data(old,_owner(after,old["document_id"]))
    assert _typed(_owner(after,out["document_id"])) == _typed(out)
    opened,report=_state(row,label+".opened"); dialog=report["dialog"]
    assert dialog["open"] is True
    if operation == "axis_range_reslice":
        assert dialog["retained_source"] is True
    assert dialog["opened_document_id"] == source["document_id"] and dialog["captured_t"] == 1 and dialog["captured_c"] == 2
    assert dialog["controls"]["apply"]["enabled"] is True
    start=row["states"][label+".before"]["frame_ordinal"]
    _clicked(row,"menu.Image",start,opened["frame_ordinal"])
    _clicked(row,"menu.Image.Stacks",start,opened["frame_ordinal"])
    action="menu.action.show_axis_plane_export" if operation == "axis_plane_export" else "menu.action.show_axis_range_reslice"
    _clicked(row,action,start,opened["frame_ordinal"])
    published,published_report=_state(row,label+".published")
    _clicked(row,"dialog.apply",opened["frame_ordinal"],published["frame_ordinal"])
    assert published_report["dialog"]["open"] is False
    assert entry["frame_ordinal"] <= published["frame_ordinal"]
    assert dialog["values"]["plane"] == request["plane"]
    if operation == "axis_plane_export":
        assert dialog["values"]["index"] == 3
    else:
        assert {key:dialog["values"][key] for key in ("first","last","step")} == {"first":1,"last":7,"step":1}
    return entry


def _compatible_case(row):
    navigated,final,_=_extended_base(row); source_id=row["source_document_id"]
    _api_labels(row,("eager.fixture","rgb.fixture"))
    assert row["files"] == {} and row["preview_observations"] == row["failed_publications"] == []
    eager=_added_fixture(row,"eager","eager_u16")
    assert row["eager_document_id"] == eager["document_id"]
    default_request=_new_request("XZ",0,6,1,t=0,c=0)
    _,dialog,_=_new_dialog(row,"eager.defaults.opened",eager["document_id"],t=0,c=0,profile="eager_u16")
    assert dialog["values"] == dict(plane="XZ",first=1,last=7,step=1,reverse_order=False,flip_vertical=False,transpose=False)
    _new_draft(row,"eager.default.xz.000.draft",default_request,eager["document_id"],profile="eager_u16")
    eager_out=_new_output(row,"eager.default.xz.000",default_request,profile="eager_u16",source_id=eager["document_id"])
    _clicked(row,"dialog.apply",row["states"]["eager.default.xz.000.draft"]["frame_ordinal"],row["states"]["eager.default.xz.000.published"]["frame_ordinal"])
    rgb=_added_fixture(row,"rgb","scientific_rgb")
    assert row["rgb_document_id"] == rgb["document_id"]
    _typed_text(row,"rgb.navigate.channel","navigation.channel","3")
    _typed_text(row,"rgb.navigate.time","navigation.time","2")
    rgb_nav,_=_state(row,"rgb.navigated")
    rgb_owner=_owner(rgb_nav,rgb["document_id"])
    assert (rgb_owner["display"]["t"],rgb_owner["display"]["c"]) == (1,2)
    _same_data(rgb,rgb_owner,display=False)
    request=_new_request("YZ",1,8,2,flags=(True,True,True))
    draft=_new_draft(row,"rgb.yz.111.draft",request,rgb["document_id"],profile="scientific_rgb")
    for field,text in (("first","2"),("last","9"),("step","2")):
        _typed_text(row,"rgb.yz.111."+field,"dialog."+field,text)
    for flag in ("reverse_order","flip_vertical","transpose"):
        _clicked(row,"dialog."+flag,row["states"]["rgb.yz.111.opened"]["frame_ordinal"],draft["frame_ordinal"])
    rgb_out=_new_output(row,"rgb.yz.111",request,profile="scientific_rgb",source_id=rgb["document_id"])
    _clicked(row,"dialog.apply",draft["frame_ordinal"],row["states"]["rgb.yz.111.published"]["frame_ordinal"])
    assert [item["label"] for item in row["outputs"]] == ["eager.default.xz.000","rgb.yz.111"]
    plane_request=dict(plane="XY",coordinate=2,t=1,c=2,max_working_bytes=CAP)
    range_request=dict(plane="XZ",first=0,last=6,step=1,t=1,c=2,max_working_bytes=CAP)
    plane=_legacy_output(row,"legacy.plane.default",plane_request,"axis_plane_export")
    ranged=_legacy_output(row,"legacy.range.default",range_request,"axis_range_reslice")
    assert [item["label"] for item in row["legacy_outputs"]] == ["legacy.plane.default","legacy.range.default"]
    assert final["workflow"] == [eager_out["command_record"],rgb_out["command_record"],plane["record"],ranged["record"]]
    assert len(final["document_ids"]) == 7 and final["active_document_id"] == ranged["output"]["document_id"]
    assert len(row["publication_observations"]) == 4
    _same_data(_owner(navigated,source_id),_owner(final,source_id))


def _refusal_case(row):
    navigated,final,_=_extended_base(row); source_id=row["source_document_id"]
    _api_labels(row,("busy.gaussian_apply_preparation","busy.orientation_refusal","malformed.original_refusal",
                     "malformed.rebind_refusal","publication.failure_preparation"))
    assert row["files"] == {} and row["preview_observations"] == row["legacy_outputs"] == []
    _new_dialog(row,"modal.opened",source_id)
    _typed_text(row,"modal.first","dialog.first","1")
    focused,focus_report=_state(row,"modal.text_focus")
    assert focus_report["input_frame"]["focus"]["active_id"] == focus_report["dialog"]["controls"]["first"]["id"] != 0
    for down,suffix in ((True,".down"),(False,".up")):
        frame=_frames_label(row,"modal.open_shortcut"+suffix)
        assert frame["requested_events"] == [{"type":"keys","keys":[{"name":"Ctrl","down":down},{"name":"O","down":down}]}]
    suppressed,suppressed_report=_state(row,"modal.shortcut_suppressed")
    _same_collection(focused,suppressed)
    assert suppressed_report["dialog"]["open"] is True
    assert suppressed_report["file_browser"]["open"] is False
    for frame in row["frames"][focused["frame_ordinal"]:suppressed["frame_ordinal"]]:
        dispatch=frame["report"]["shortcuts"]["dispatch"]
        assert dispatch["callback_completed"] is False and dispatch["command_id"] is None
        assert dispatch["focus"] == "modal"
    valid,valid_dialog,_=_new_dialog(row,"busy.valid_draft",source_id)
    assert valid_dialog["text"]["first"] == "1" and valid_dialog["can_apply"] is True
    assert valid_dialog["controls"]["apply"]["enabled"] is True and valid["busy"] is False
    prepared=_api(row,"busy.gaussian_apply_preparation")
    assert prepared["inputs"] == dict(method="Window.applyGaussian",positions_tcz=[[1,2,2]],roi="whole_plane",sigma=.85)
    busy,busy_dialog,_=_new_dialog(row,"busy.entered",source_id)
    after,after_report=_state(row,"busy.disabled_submit")
    assert busy["busy"] is after["busy"] is True
    assert busy_dialog["can_apply"] is True and busy_dialog["controls"]["apply"]["enabled"] is False
    assert after_report["dialog"]["can_apply"] is True and after_report["dialog"]["controls"]["apply"]["enabled"] is False
    _clicked(row,"dialog.apply",busy["frame_ordinal"],after["frame_ordinal"],disabled=True)
    _same_collection(busy,after)
    busy_observation=row["busy_preparation"]
    assert busy_observation["scope"] == "Disclosed genuine scientific Gaussian Apply at first positive source-copy checkpoint; not orientation processing or physical Apply credit"
    probe=busy_observation["source_copy_probe"]
    assert probe["source_id"] == source_id and probe["operation"] == "gaussian_blur_imagej"
    assert probe["stage"] == "ImageJ Gaussian source copy"
    assert 0 < _integer(probe["completed"],1) <= _integer(probe["total"],1)
    _job(busy_observation["job_before"],source_id,1)
    _job(busy_observation["job_after"],source_id,6)
    assert busy_observation["job_before"]["id"] == busy_observation["job_after"]["id"]
    assert busy_observation["job_after"]["cancel_requested"] is True
    refusal=_api(row,"busy.orientation_refusal",accepted=False,category="invalid_argument")
    assert refusal["inputs"] == dict(method="Window.resliceOrientedAxisRange",request=_new_request())
    released,_=_state(row,"busy.released")
    assert released["busy"] is False and released["usage"] == ZERO_USAGE
    cancelled,cancelled_report=_state(row,"modal.cancelled")
    _clicked(row,"dialog.cancel",released["frame_ordinal"],cancelled["frame_ordinal"])
    assert cancelled_report["dialog"]["open"] is False
    original=_api(row,"malformed.original_refusal",accepted=False,category="invalid_argument")
    rebound=_api(row,"malformed.rebind_refusal",accepted=False,category="invalid_argument")
    malformed=original["inputs"]["record"]
    assert original["inputs"]["method"] == rebound["inputs"]["method"] == "Window.action.replay_axis_orientation"
    assert rebound["inputs"]["record"] == malformed
    assert rebound["inputs"]["rebind_request"] == dict(_new_request(),operation="axis_range_orientation")
    assert malformed["derived"]["working_bytes"] == 1
    corrected=dict(malformed,derived=dict(malformed["derived"],working_bytes=_layout_for(_new_request(),"scientific_u16")[0]["working_bytes"]))
    _wire_record(corrected,_owner(original["before"],source_id),_new_request(),"scientific_u16")
    failed_draft=_new_draft(row,"failed.draft",_new_request(),source_id)
    for field,text in (("first","2"),("last","5"),("step","2")):
        _typed_text(row,"failed."+field,"dialog."+field,text)
    failed,failed_report=_state(row,"failed.publication_refused")
    _clicked(row,"dialog.apply",failed_draft["frame_ordinal"],failed["frame_ordinal"])
    assert failed["window_error"] and failed_report["error_modal"]["open"] is True
    assert failed_report["dialog"]["open"] is True
    assert len(row["failed_publications"]) == 1
    rejected=row["failed_publications"][0]
    assert rejected["label"] == "failed.xy.000" and rejected["trigger"] == "physical_shared_imgui" and rejected["api_label"] == ""
    assert rejected["scope"] == "Real failed publication; no after_attach or successful output is invented"
    assert rejected["request"] == _new_request() and rejected["window_error"] == failed["window_error"]
    _wire_record(rejected["command_record"],rejected["source_before"],_new_request(),"scientific_u16")
    _same_data(rejected["source_before"],rejected["source_after"])
    _same_collection(rejected["state_before"],rejected["state_after"],error=False)
    mutation=rejected["candidate_mutation"]
    assert mutation["scope"] == "Disclosed public native API fixture at existing prepublication observation seam; not ordinary UI behavior"
    assert mutation["method"] == "DocumentTransaction.set_property+Document.commit"
    candidate_before,candidate_after=mutation["before"],mutation["after"]
    _fresh_output(candidate_before,rejected["source_before"],_new_request(),"scientific_u16")
    for key in ("document_id","shape","dtype","sample_policy","calibration","selection","previous_selection","rois","overlays","spectrum_present","planes","display"):
        assert _typed(candidate_before[key]) == _typed(candidate_after[key])
    assert candidate_after["metadata"] == dict(candidate_before["metadata"],properties={"owned_publication_refusal":True})
    assert candidate_after["revisions"]["metadata"] == candidate_before["revisions"]["metadata"]+1
    for key in ("pixels","selection","display"):
        assert candidate_after["revisions"][key] == candidate_before["revisions"][key]
    assert candidate_after["dirty"] is True and candidate_after["history"]["undo_steps"] == 1 and candidate_after["history"]["redo_steps"] == 0
    assert candidate_after["data_identity"] != candidate_before["data_identity"]
    assert candidate_after["document_id"] not in failed["document_ids"]+failed["facade_ids"]+final["document_ids"]
    assert failed["document_ids"] == [source_id] and failed["workflow"] == [] and failed["results"] == []
    acknowledged,ack_report=_state(row,"failed.error_acknowledged")
    _clicked(row,"error_modal.ok",failed["frame_ordinal"],acknowledged["frame_ordinal"])
    assert acknowledged["window_error"] == "" and ack_report["error_modal"]["open"] is False
    _new_draft(row,"recovery.xy.000.draft",_new_request(),source_id)
    recovered=_new_output(row,"recovery.xy.000",_new_request())
    _clicked(row,"dialog.apply",acknowledged["frame_ordinal"],row["states"]["recovery.xy.000.published"]["frame_ordinal"])
    assert [item["label"] for item in row["outputs"]] == ["recovery.xy.000"]
    assert [(item["label"],item["phase"]) for item in row["publication_observations"]] == [
        ("failed.xy.000","before_native_publish"),("recovery.xy.000","before_native_publish"),("recovery.xy.000","after_attach")]
    assert final["document_ids"] == [source_id,recovered["output"]["document_id"]]
    assert final["workflow"] == [recovered["command_record"]] and final["active_document_id"] == recovered["output"]["document_id"]
    _same_data(_owner(navigated,source_id),_owner(final,source_id))


@pytest.mark.parametrize(("case_id", "context"), PARAMETERS, ids=PARAMETER_IDS)
def test_installed_scientific_orientation(case_id, context):
    row = _load(case_id,context)
    if case_id == "O01":
        _cancel_case(row)
    elif case_id == "O06":
        _invalid_case(row)
    elif case_id == "O07":
        _capture_lifetime_case(row)
    elif case_id == "O08":
        _workflow_replay_case(row)
    elif case_id == "O09":
        _workflow_rebind_case(row)
    elif case_id == "O10":
        _preview_case(row)
    elif case_id == "O11":
        _compatible_case(row)
    elif case_id == "O12":
        _refusal_case(row)
    else:
        _positive_case(row)
    _recheck()
