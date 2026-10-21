"""Strict, product-free extraction of the scoped saved-ROI API observations.

This compares actual scalar values with the six frozen surface contracts. It
does not qualify an installation, create a test result, or adopt parity proof.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import sys


MAX_RAW_BYTES = 16 * 1024 * 1024
MAX_TOTAL_RAW_BYTES = 16 * 1024 * 1024
MAX_COMMAND_BYTES = 8 * 1024 * 1024
MAX_OUTPUT_BYTES = 1024 * 1024
MAX_SOURCE_BYTES = 128 * 1024
CONTEXTS = ("cpp_api", "python_api")
PREFIX = "G015.saved-roi-geometry."
CONTRACT_HASHES = {
    "geometry": "2daaf6fab50688765088d4be9d62bfcb3d0370e7a43bda6d4b502759a12b6004",
    "targets": "3d37c167a45b58cbabb9568ab24714f971df27c1b390bbcedac9f294455c69ab",
    "publication": "ef79b7a8eada582413dc11b47ca3a2d533de1182be79f4ce7c1382f4af4ded0b",
    "resources": "f1473f98058e1d3210dcb14c03c0a5581a320ad7d41df20cc6d05f84d03eee28",
    "record-replay-rebind": "6db731b9fddb0bc2c3e84e6d07eaea7753c65b0d2bbbbf0d5b4d19faaa60336d",
    "persistence-surfaces": "48035e1d846e4a6ca9c61f105af7079b34636af23a0a6307b7194c55ff0deae0",
}
SDK_CASES = (
    "S01.literal_row_geometry_and_zero_read",
    "S02.prepared_publication_atomic_guards",
    "S03.strict_record_replay_then_stale_repeat",
    "S04.explicit_rebind_and_v3_reopen",
    "S05.caps_cancel_noop_and_source_preservation",
)
HOST_CASES = (
    "H01.explicit_row_entry_keeps_global_selection",
    "H02.target_navigation_zero_wildcard_and_capture",
    "H03.private_valid_invalid_escape_last_drag",
    "H04.metadata_selection_history_recording_and_reselection",
    "H05.ordinary_and_linked_xy_target_render_generation",
    "H06.stale_activation_tcz_display_busy_close",
    "H07.workflow_matching_replay_stale_repeat_complete_rebind",
    "H08.native_v3_fresh_owner_and_legacy_guards",
)
PYTHON_CASES = (
    "A01.typed_request_detached_values",
    "A02.area_geometry_and_row_preservation",
    "A03.bool_lossy_overflow_unknown_refusal",
    "A04.private_prepare_no_mutation_and_exact_noop",
    "A05.atomic_active_owner_capture",
    "A06.stale_all_data_and_tcz",
    "A07.pointer_display_numeric_camera",
    "A08.strict_record_replay_and_stale_repeat",
    "A09.full_rebind_invalid_original",
    "A10.reopen_owner_and_native_v3",
    "A11.control_limits_callback_and_zero_reads",
    "A12.desktop_binding_wrapper_same_controller_recording",
)


def _ids(text):
    return {"saved_roi." + suffix for suffix in text.split()}


# These are source-case bindings, not expected values or case-count evidence.
SDK_GROUPS = (
    _ids("""rectangle.moved_points geometry.preserved_width geometry.attributes_preserved
        position.wildcard_constraint_count entry.global_selection_changes mask.translated_members
        target.row_attributes_preserved target.provider_reads provider.read_plane_calls
        provider.read_region_calls changed.metadata_revision_delta changed.selection_revision_delta
        changed.pixels_revision_delta changed.undo_steps_delta changed.redo_steps_after
        mask.rotated_members mask.clipped_members mask.empty_handle_count rectangle.numeric_points"""),
    _ids("""numeric.fresh_camera_preserved undo_redo.pixels_revision_delta
        entry.owner_roundtrip_publication_delta failure.native_state_unchanged
        constraint_rejection.publication_delta"""),
    _ids("""entry.hidden_fresh_refused record.operation position.explicit_zero_tcz
        strict_replay.metadata_revision_delta strict_replay.fresh_navigation_preserved
        strict_repeat.metadata_revision_delta"""),
    _ids("""reopen.saved_row_count_delta reopen.undo_steps reopen.redo_steps
        reopen.native_geometry_preserved reopen.explicit_zero_and_null_distinct
        reopened_owner.strict_replay_publication_delta full_rebind.metadata_revision_delta
        rebind.explicit_target_preserved malformed_original.publication_delta"""),
    _ids("""malformed.callback_count invalid_request.publication_delta pre_cancel.callback_count
        terminal.retained_working_bytes failure.metadata_revision_delta cancel.document_dirty_changes
        control.captured_values_preserved noop.undo_steps_delta"""),
)
HOST_GROUPS = (set(), set(), set(),
               _ids("recording_off.workflow_append_delta"),
               _ids("linked_xzyz.raw_samples_preserved"), set(), set(), set())
PYTHON_GROUPS = (
    _ids("""rectangle.moved_points geometry.preserved_width geometry.attributes_preserved
        position.wildcard_constraint_count entry.global_selection_changes target.row_attributes_preserved"""),
    _ids("""entry.hidden_fresh_refused position.explicit_zero_tcz mask.translated_members
        mask.rotated_members mask.clipped_members mask.empty_handle_count rectangle.numeric_points
        changed.metadata_revision_delta changed.selection_revision_delta changed.pixels_revision_delta
        changed.undo_steps_delta changed.redo_steps_after undo_redo.pixels_revision_delta"""),
    _ids("invalid_request.publication_delta"),
    _ids("noop.undo_steps_delta terminal.retained_working_bytes"),
    _ids("entry.owner_roundtrip_publication_delta"),
    _ids("failure.native_state_unchanged"),
    _ids("numeric.fresh_camera_preserved"),
    _ids("""record.operation strict_replay.metadata_revision_delta
        strict_replay.fresh_navigation_preserved strict_repeat.metadata_revision_delta"""),
    _ids("""full_rebind.metadata_revision_delta rebind.explicit_target_preserved
        malformed.callback_count malformed_original.publication_delta"""),
    _ids("""reopen.saved_row_count_delta reopen.undo_steps reopen.redo_steps
        reopen.native_geometry_preserved reopen.explicit_zero_and_null_distinct
        reopened_owner.strict_replay_publication_delta linked_xzyz.raw_samples_preserved"""),
    _ids("""pre_cancel.callback_count control.captured_values_preserved cancel.document_dirty_changes
        provider.read_plane_calls provider.read_region_calls target.provider_reads
        constraint_rejection.publication_delta failure.metadata_revision_delta"""),
    _ids("recording_off.workflow_append_delta"),
)


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, allow_nan=False).encode("utf-8")


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        _require(key not in result, "Duplicate JSON object key: " + key)
        result[key] = value
    return result


def _finite_number(text):
    value = float(text)
    _require(math.isfinite(value), "Nonfinite JSON number")
    return value


def _invalid_constant(text):
    raise ValueError("Invalid JSON constant: " + text)


def _legacy_manifest_constant(text):
    # Compatibility for one unconsumed imported descriptor, admitted by its
    # complete path and identity below. Raw observations never use this parser.
    _require(text == "Infinity", "Invalid manifest JSON constant: " + text)
    return math.inf


def _legacy_priority_path(value):
    path = ("candidates", 966, "descriptor", "values", "priority")
    candidates = value.get("candidates") if type(value) is dict else None
    if type(candidates) is not list or len(candidates) <= 966:
        return None
    row = candidates[966]
    if type(row) is not dict:
        return None
    descriptor = row.get("descriptor")
    if type(descriptor) is not dict or type(descriptor.get("values")) is not dict:
        return None
    values = descriptor["values"]
    if (row.get("id") == "R-54a632549fbc1c07df466cce" and
            row.get("component_id") == "fiji:jars/imagej-plugins-tools-0.3.1.jar" and
            row.get("kind") == "scijava_plugin" and
            descriptor.get("class") == "net.imagej.plugins.tools.KonamiHandler" and
            values.get("name") == "Konami" and values.get("type") == "org.scijava.tool.Tool"):
        return path
    return None


def _structure(value, max_nodes, *, legacy_manifest=False):
    allowed = _legacy_priority_path(value) if legacy_manifest else None
    pending, nodes = [(value, 0, ())], 0
    while pending:
        item, depth, path = pending.pop()
        nodes += 1
        _require(nodes <= max_nodes and depth <= 64, "JSON structural bound exceeded")
        if type(item) is dict:
            pending.extend((child, depth + 1, path + (key,)) for key, child in item.items())
        elif type(item) is list:
            pending.extend((child, depth + 1, path + (index,)) for index, child in enumerate(item))
        elif type(item) is float:
            _require(math.isfinite(item) or (allowed is not None and path == allowed and item == math.inf),
                     "Nonfinite JSON scalar outside the isolated legacy descriptor priority")


def _file(path):
    path = Path(path)
    _require(not path.is_symlink(), "Symlink input is not admitted: " + str(path))
    path = path.resolve(strict=True)
    _require(path.is_file(), "A regular input file is required: " + str(path))
    return path


def _read(path, limit, *, max_nodes=4096, legacy_manifest=False):
    path = _file(path)
    before = path.stat()
    _require(0 < before.st_size <= limit, "Input byte bound exceeded: " + str(path))
    with path.open("rb") as stream:
        body = stream.read(limit + 1)
    after = path.stat()
    _require(len(body) == before.st_size == after.st_size and
             before.st_mtime_ns == after.st_mtime_ns and len(body) <= limit,
             "Input changed while reading: " + str(path))
    pin = {"path": str(path), "bytes": len(body), "sha256": hashlib.sha256(body).hexdigest()}
    try:
        value = json.loads(body.decode("utf-8"), object_pairs_hook=_unique_object,
                           parse_float=_finite_number,
                           parse_constant=_legacy_manifest_constant if legacy_manifest else _invalid_constant)
    except (UnicodeError, RecursionError) as error:
        raise ValueError("Invalid bounded UTF-8 JSON: " + str(path)) from error
    _structure(value, max_nodes, legacy_manifest=legacy_manifest)
    return value, pin


def _pin(path, limit):
    path = _file(path)
    size = path.stat().st_size
    _require(0 < size <= limit, "Pinned file byte bound exceeded: " + str(path))
    digest, count = hashlib.sha256(), 0
    with path.open("rb") as stream:
        while block := stream.read(65536):
            count += len(block)
            _require(count <= limit, "Pinned file grew beyond bound: " + str(path))
            digest.update(block)
    _require(count == size == path.stat().st_size, "Pinned file changed: " + str(path))
    return {"path": str(path), "bytes": count, "sha256": digest.hexdigest()}


def _keys(value, keys, label):
    _require(type(value) is dict and set(value) == set(keys), label + " fields differ")


def _typed(value, value_type, label):
    types = {"boolean": bool, "integer": int, "string": str}
    _require(value_type in types and type(value) is types[value_type], label + " scalar type differs")


def _contracts(path):
    commands, pin = _read(path, MAX_COMMAND_BYTES, max_nodes=1000000, legacy_manifest=True)
    _require(type(commands) is dict and type(commands.get("surface_contracts")) is list,
             "Commands has no surface contract catalogue")
    wanted = {"SC." + PREFIX + stem for stem in CONTRACT_HASHES}
    found = {}
    for contract in commands["surface_contracts"]:
        _require(type(contract) is dict and type(contract.get("id")) is str,
                 "Malformed surface contract catalogue row")
        if contract["id"] not in wanted:
            continue
        _require(contract["id"] not in found, "Duplicate saved-ROI surface contract")
        _keys(contract, ("id", "requirement_id", "snapshot", "contexts", "sha256"), "Contract")
        _require(type(contract["requirement_id"]) is str, "Contract requirement identity must be a string")
        stem = contract["requirement_id"].removeprefix(PREFIX)
        _require(stem in CONTRACT_HASHES and contract["id"] == "SC." + PREFIX + stem,
                 "Saved-ROI contract identity differs")
        frozen = {key: contract[key] for key in ("id", "requirement_id", "snapshot", "contexts")}
        _require(contract["sha256"] == CONTRACT_HASHES[stem] == hashlib.sha256(_canonical(frozen)).hexdigest(),
                 "Saved-ROI contract hash differs: " + stem)
        _require(type(contract["contexts"]) is dict and set(contract["contexts"]) ==
                 {"cpp_api", "python_api", "imgui_cpp", "imgui_python"}, "Contract contexts differ")
        for context in CONTEXTS:
            assertions = contract["contexts"][context]
            _require(type(assertions) is list and assertions, "Missing API contract assertions")
            for assertion in assertions:
                _keys(assertion, ("id", "rule", "value_type", "expected", "observation"), "Assertion")
                _require(type(assertion["id"]) is str and assertion["id"].startswith("saved_roi.") and
                         assertion["rule"] == "scalar_equal" and
                         type(assertion["observation"]) is str and assertion["observation"],
                         "Unsupported saved-ROI assertion")
                _typed(assertion["expected"], assertion["value_type"], assertion["id"])
        found[contract["id"]] = contract
    _require(set(found) == wanted, "Missing saved-ROI surface contracts")
    result = [found["SC." + PREFIX + stem] for stem in CONTRACT_HASHES]
    for context in CONTEXTS:
        ids = [a["id"] for c in result for a in c["contexts"][context]]
        _require(len(ids) == len(set(ids)) == 49, "API contract assertion membership differs")
    return result, pin


def _raw(path, producer_id, cases, groups):
    raw, pin = _read(path, MAX_RAW_BYTES)
    _keys(raw, ("schema_version", "producer_id", "complete", "cases"), "Raw envelope")
    _require(type(raw["schema_version"]) is int and raw["schema_version"] == 1 and
             raw["producer_id"] == producer_id and raw["complete"] is True,
             "Incomplete or wrong raw producer: " + str(path))
    _require(type(raw["cases"]) is list and len(raw["cases"]) == len(cases), "Raw case count differs")
    values, provenance = {}, []
    for row, case_id, required in zip(raw["cases"], cases, groups):
        _keys(row, ("id", "observations"), "Raw case")
        _require(row["id"] == case_id and type(row["observations"]) is list and
                 len(row["observations"]) == len(required), "Raw case identity or observations differ: " + case_id)
        seen = set()
        for observation in row["observations"]:
            _keys(observation, ("id", "encoding", "value"), "Raw observation")
            identifier = observation["id"]
            _require(type(identifier) is str and identifier in required and identifier not in seen and
                     identifier not in values and observation["encoding"] == "scalar",
                     "Missing/duplicate/extra/non-scalar observation in " + case_id)
            _require(type(observation["value"]) in (bool, int, str), "Unsupported raw scalar type: " + identifier)
            seen.add(identifier)
            values[identifier] = {"encoding": observation["encoding"], "value": observation["value"],
                                  "source": {"producer_id": producer_id, "case_id": case_id,
                                             "observation_id": identifier, "raw_input": pin}}
        _require(seen == required, "Raw observation membership differs: " + case_id)
        provenance.append({"id": case_id, "raw_input": pin,
                           "observation_ids": [o["id"] for o in row["observations"]]})
    return values, provenance, pin


def _python_files(directory, names):
    actual_names = set()
    for entry in directory.iterdir():
        _require(entry.name in names and entry.name not in actual_names and
                 not entry.is_symlink() and entry.is_file(), "Unexpected Python raw directory entry")
        actual_names.add(entry.name)
    _require(actual_names == names, "Python observation file membership differs")


def extract(cpp_sdk, cpp_host, python_directory):
    """Read only the fixed native/Python carriers; return detached comparisons."""
    source_path = _file(__file__)
    source_pin = _pin(source_path, MAX_SOURCE_BYTES)
    contracts, commands_pin = _contracts(source_path.parent.parent / "docs/parity/commands.json")
    directory = Path(python_directory)
    _require(not directory.is_symlink(), "Python observation directory cannot be a symlink")
    directory = directory.resolve(strict=True)
    _require(directory.is_dir(), "Python observation directory is required")
    names = {hashlib.sha256(case.encode("utf-8")).hexdigest() + ".json" for case in PYTHON_CASES}
    _python_files(directory, names)
    sdk_path, host_path = _file(cpp_sdk), _file(cpp_host)
    python_paths = [_file(directory / (hashlib.sha256(case.encode("utf-8")).hexdigest() + ".json"))
                    for case in PYTHON_CASES]
    paths = [sdk_path, host_path, *python_paths]
    sizes = [path.stat().st_size for path in paths]
    _require(len(set(paths)) == 14 and all(0 < size <= MAX_RAW_BYTES for size in sizes) and
             sum(sizes) <= MAX_TOTAL_RAW_BYTES, "Aggregate raw input bound exceeded before parsing")
    sdk, sdk_cases, sdk_pin = _raw(sdk_path, "p06g.saved-roi.sdk.raw.001", SDK_CASES, SDK_GROUPS)
    host, host_cases, host_pin = _raw(host_path, "p06g.saved-roi.host.raw.001", HOST_CASES, HOST_GROUPS)
    python, python_cases, python_pins = {}, [], []
    for case_id, required, path in zip(PYTHON_CASES, PYTHON_GROUPS, python_paths):
        values, cases, pin = _raw(path, "p06g.saved-roi.python.raw.001", (case_id,), (required,))
        _require(not (python.keys() & values.keys()), "Duplicate Python observation across files")
        python.update(values)
        python_cases.extend(cases)
        python_pins.append(pin)
    raw_pins = [sdk_pin, host_pin, *python_pins]
    _require(len({pin["path"] for pin in raw_pins}) == 14 and
             sum(pin["bytes"] for pin in raw_pins) <= MAX_TOTAL_RAW_BYTES, "Aggregate raw input bound exceeded")
    _require(not (sdk.keys() & host.keys()) and len(sdk) == 47 and len(host) == 2,
             "C++ producer observation ownership differs")
    cpp = {**sdk, **host}
    output_contexts = {}
    for context, values in (("cpp_api", cpp), ("python_api", python)):
        required = {a["id"] for contract in contracts for a in contract["contexts"][context]}
        _require(set(values) == required and len(values) == 49, "Context observation membership differs: " + context)
        surfaces = []
        for contract in contracts:
            comparisons = []
            for assertion in contract["contexts"][context]:
                observed = values[assertion["id"]]
                _typed(observed["value"], assertion["value_type"], context + ":" + assertion["id"])
                _require(observed["value"] == assertion["expected"],
                         "Observed scalar differs from contract: " + context + ":" + assertion["id"])
                comparisons.append({**assertion, "encoding": observed["encoding"],
                                    "observed": observed["value"], "source": observed["source"]})
            surfaces.append({"requirement_id": contract["requirement_id"], "contract_id": contract["id"],
                             "contract_sha256": contract["sha256"], "assertions": comparisons})
        output_contexts[context] = surfaces
    pins = [(source_pin, MAX_SOURCE_BYTES), (commands_pin, MAX_COMMAND_BYTES),
            *((pin, MAX_RAW_BYTES) for pin in raw_pins)]
    for pin, limit in pins:
        _require(_pin(pin["path"], limit) == pin, "Consumed input changed: " + pin["path"])
    _python_files(directory, names)
    return {"schema_version": 1, "kind": "p06g.saved-roi-api-surface-extractions.001",
            "complete": True, "acceptance_claim": False,
            "inputs": {"extractor": source_pin, "commands": commands_pin, "cpp_sdk": sdk_pin,
                       "cpp_host": host_pin, "python_raw": python_pins},
            "producers": [
                {"id": "p06g.saved-roi.sdk.raw.001", "context": "cpp_api", "cases": sdk_cases},
                {"id": "p06g.saved-roi.host.raw.001", "context": "cpp_api", "cases": host_cases},
                {"id": "p06g.saved-roi.python.raw.001", "context": "python_api", "cases": python_cases}],
            "contexts": output_contexts}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp-sdk", required=True, type=Path)
    parser.add_argument("--cpp-host", required=True, type=Path)
    parser.add_argument("--python-directory", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    _require(args.output.name not in ("", ".", ".."), "A fresh output filename is required")
    parent = args.output.parent.resolve(strict=True)
    _require(parent.is_dir(), "Output parent must be an existing directory")
    output = parent / args.output.name
    _require(not os.path.lexists(output), "Output must be fresh")
    result = extract(args.cpp_sdk, args.cpp_host, args.python_directory)
    payload = _canonical(result) + b"\n"
    _require(len(payload) <= MAX_OUTPUT_BYTES, "Extracted output exceeds1MiB")
    with output.open("xb") as stream:
        _require(stream.write(payload) == len(payload), "Incomplete extraction output write")
        stream.flush()
        os.fsync(stream.fileno())


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as error:
        print("saved-ROI surface extraction refused: " + str(error), file=sys.stderr)
        raise SystemExit(1) from error
