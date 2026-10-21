"""Compose existing Auto observations into four B2 cases; never run the product.

CLI: python -I -X utf8 -B scripts/compose_auto_contrast_evidence.py
     --manifest <workspace-relative-json> --output <fresh-workspace-directory>

The strict input manifest has schema_version:1, run_id, output_directory,
surface_contract:{path,sha256}, consumer_contract:{path,sha256}, tool_sources:
[{path,sha256}], and contexts:{cpp_api,python_api,imgui_cpp,imgui_python}.
Each context has an existing B2 producer {id,context,sources,artifacts} and
contributors:[{id,kind,case_id,report,observations}]. kind is "envelope" or
"ui"; observations is a pinned stdout or raw case.json. report has exact
file:{path,sha256}, format (ctest_log_v1/pytest_junit_v1), role and
case_identity. UI contexts require U01/U07/U08 and use only kind:"ui".
API contexts require actual AUTO_OBSERVATION_JSON stdout envelopes.
The internal-only supervised_auto_native_v1 contributor format additionally
accepts the pinned p06c4.native-qualified.001 receipt with role:"native" and
case_identity:{name:"A01.strict_cycle"|"A17.setting_capture_override_reopen"}.
It verifies its actual native step/stdout/source/executable, all64 named cases
and both matching envelopes; it is not a global parity report format.

All tools, contributor reports/observations and consumed files must also occur
in the context producer's frozen source/artifact sets. All paths are safe
workspace-relative pins; stdout's persisted_file may additionally be an
absolute path inside this workspace, resolving to that same pinned identity.
Envelopes are schema_version:1, context, case_id, observations:{id:value},
artifacts:{} or {persisted_file:path}. No observed value is supplied by the
manifest or copied from a contract expectation.

Outputs are one composed JUnit report, one existing-schema trace/context and
a proposed trace catalogue plus contributor receipts. They certify successful
composition checks only, and write no acceptance/manifest/proof records.
16MiB/file, 64MiB unique input bytes, four contexts, eight contributors/context.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import importlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import sys
import xml.etree.ElementTree as ET


MAX_INPUT = 16 * 1024**2
MAX_TOTAL = 64 * 1024**2
CONTEXTS = ("cpp_api", "python_api", "imgui_cpp", "imgui_python")
SCENARIOS = ("default", "changed", "frozen", "override", "persisted")
SETTING = "conversion_unsigned16_range"
PREFIX = re.compile(r"^(?:\s*\d+:\s)?AUTO_OBSERVATION_JSON (.+)$")
TOOLS = (
    "scripts/compose_auto_contrast_evidence.py",
    "scripts/parity_consumers.py",
    "scripts/parity_execution.py",
    "scripts/parity_evidence.py",
    "scripts/parity_schema.py",
    "tests/python/test_auto_contrast_ui.py",
    "tests/python/test_channel_display_ui.py",
    "tests/python/test_channel_display_projection_ui.py",
    "tests/python/test_composite_max_reference.py",
)
REPORT_SUITE = "p06c4_auto_composed"
REPORT_CLASS = "imagejxx.parity.auto_contrast"


class CompositionError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise CompositionError(message)


def exact(value, keys, where):
    require(type(value) is dict and set(value) == set(keys),
            f"{where}: requires exact fields {','.join(keys)}")
    return value


def text(value, where):
    require(type(value) is str and bool(value), f"{where}: nonempty string required")
    return value


def strict_json(raw, where):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, f"{where}: duplicate JSON key {key}")
            result[key] = value
        return result

    def nonfinite(value):
        raise CompositionError(f"{where}: nonfinite JSON value {value}")

    try:
        result = json.loads(raw.decode("utf-8-sig"), object_pairs_hook=pairs,
                            parse_constant=nonfinite)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise CompositionError(f"{where}: invalid bounded UTF8 JSON: {error}") from error
    finite(result, where)
    return result


def finite(value, where, depth=0):
    require(depth <= 128, f"{where}: JSON nesting exceeds128")
    if type(value) is dict:
        for key, item in value.items():
            require(type(key) is str, f"{where}: string JSON key required")
            finite(item, where, depth + 1)
    elif type(value) is list:
        for item in value:
            finite(item, where, depth + 1)
    elif type(value) is float:
        require(math.isfinite(value), f"{where}: finite JSON number required")
    else:
        require(type(value) in (str, int, bool, type(None)),
                f"{where}: unsupported JSON value")


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def encode(value):
    return (json.dumps(value, sort_keys=True, ensure_ascii=False, indent=2,
                       allow_nan=False) + "\n").encode("utf-8")


class Inputs:
    def __init__(self, root):
        self.root = root.resolve(strict=True)
        self.cache = {}
        self.total = 0

    def path(self, relative):
        text(relative, "path")
        pure = PurePosixPath(relative)
        require("\\" not in relative and ":" not in relative and
                not pure.is_absolute() and all(part not in ("", ".", "..")
                                              for part in relative.split("/")),
                f"unsafe workspace-relative path: {relative}")
        target = self.root.joinpath(*pure.parts)
        node = target
        while node != self.root:
            require(not node.is_symlink() and
                    not getattr(node, "is_junction", lambda: False)(),
                    f"aliased input/output path: {relative}")
            node = node.parent
        require(target.resolve().is_relative_to(self.root),
                f"path escapes workspace: {relative}")
        return target

    def pin(self, value, where):
        exact(value, ("path", "sha256"), where)
        require(type(value["sha256"]) is str and
                re.fullmatch("[0-9a-f]{64}", value["sha256"]) is not None,
                f"{where}: SHA256 required")
        target = self.path(value["path"])
        if value["path"] in self.cache:
            raw = self.cache[value["path"]]
        else:
            require(target.is_file(), f"{where}: missing file {value['path']}")
            count = target.stat().st_size
            require(0 <= count <= MAX_INPUT,
                    f"{where}: input exceeds16MiB: {value['path']}")
            require(self.total + count <= MAX_TOTAL,
                    f"{where}: unique input bytes exceed64MiB")
            with target.open("rb") as stream:
                raw = stream.read(MAX_INPUT + 1)
            require(len(raw) == count and len(raw) <= MAX_INPUT,
                    f"{where}: input size changed/exceeds16MiB")
            self.cache[value["path"]] = raw
            self.total += len(raw)
        require(sha(raw) == value["sha256"], f"{where}: stale file {value['path']}")
        return raw

    def json(self, pin, where):
        return strict_json(self.pin(pin, where), where)

    def actual_artifact(self, value, artifacts, where):
        path = Path(text(value, where))
        if path.is_absolute():
            require(path.resolve().is_relative_to(self.root),
                    f"{where}: artifact outside workspace")
            value = path.resolve().relative_to(self.root).as_posix()
        self.path(value)
        require(value in artifacts, f"{where}: consumed file lacks producer artifact pin: {value}")
        pin = artifacts[value]
        self.pin(pin, where)
        return pin


def pinned_map(values, inputs, where):
    require(type(values) is list and 1 <= len(values) <= 256,
            f"{where}: requires1..256 pins")
    result = {}
    for pin in values:
        inputs.pin(pin, where)
        require(pin["path"] not in result, f"{where}: duplicate path {pin['path']}")
        result[pin["path"]] = pin
    return result


def load_tools(inputs, pins):
    known = pinned_map(pins, inputs, "tool_sources")
    require(set(TOOLS) <= known.keys(), "tool_sources: missing composer/validator/reader source pin")
    for relative in TOOLS:
        inputs.pin(known[relative], "tool source")
    # Only existing validator and independent reader helpers; never imagejxx or
    # installed fixtures, runtime capture, subprocesses or product processing.
    sys.path.insert(0, str(inputs.root / "scripts"))
    consumers = importlib.import_module("parity_consumers")
    for relative, module in (
            ("scripts/parity_consumers.py", consumers),
            ("scripts/parity_execution.py", consumers.execution),
            ("scripts/parity_evidence.py", consumers.execution.evidence),
            ("scripts/parity_schema.py", consumers.execution.schema)):
        require(Path(module.__file__).resolve() == inputs.root / relative,
                f"validator imported from unexpected path: {relative}")
    sys.path.insert(0, str(inputs.root / "tests/python"))
    reader = importlib.import_module("test_auto_contrast_ui")
    modules = {
        "tests/python/test_auto_contrast_ui.py": reader,
        "tests/python/test_channel_display_ui.py": reader.base,
        "tests/python/test_channel_display_projection_ui.py": reader.projection,
        "tests/python/test_composite_max_reference.py": reader.projection.reference,
    }
    for relative, module in modules.items():
        require(Path(module.__file__).resolve() == inputs.root / relative,
                f"independent reader imported from unexpected path: {relative}")
    require(not any(name == "imagejxx" or name.startswith("imagejxx.")
                    for name in sys.modules), "composer must not import product modules")
    return consumers, reader, known


def supervised_auto_native_report(contributor, context, inputs, sources, artifacts):
    """Read only the existing direct native execution; do not invent a CTest run."""
    report = contributor["report"]
    require(context == "cpp_api" and contributor["kind"] == "envelope" and
            report["role"] == "native",
            "supervised Auto native receipt requires cpp_api/native envelope")
    case_names = {"A01": "A01.strict_cycle",
                  "A17": "A17.setting_capture_override_reopen"}
    require(contributor["case_id"] in case_names,
            "supervised Auto native supports only actual A01/A17 contributors")
    exact(report["case_identity"], ("name",), "supervised native case identity")
    require(report["case_identity"]["name"] == case_names[contributor["case_id"]],
            "supervised native report case/envelope identity differs")
    receipt = inputs.json(report["file"], contributor["id"] + ".native_receipt")
    require(type(receipt) is dict and receipt.get("id") == "p06c4.native-qualified.001" and
            receipt.get("state") == "passed_native_carrier_and_installed_sdk",
            "unsupported or nonpassing supervised Auto native receipt")

    def bound_file(value, pins, where):
        exact(value, ("path", "bytes", "sha256"), where)
        require(type(value["bytes"]) is int and 0 <= value["bytes"] <= MAX_INPUT,
                where + ": invalid bounded byte count")
        pin = {key: value[key] for key in ("path", "sha256")}
        require(pins.get(pin["path"]) == pin,
                where + ": missing/different frozen producer pin")
        raw = inputs.pin(pin, where)
        require(len(raw) == value["bytes"], where + ": receipt byte count differs")
        return pin, raw

    native = receipt.get("steps", {}).get("native")
    exact(native, ("argv", "timeout_seconds", "exit_code", "stdout", "stderr"),
          "supervised native step")
    require(type(native["exit_code"]) is int and native["exit_code"] == 0 and
            type(native["timeout_seconds"]) is int and native["timeout_seconds"] == 60,
            "supervised native step failed or changed its60-second bound")
    require(type(native["argv"]) is list and len(native["argv"]) == 1,
            "supervised native must directly execute one test executable without arguments")
    declared = [row for row in receipt.get("commands", [])
                if type(row) is dict and row.get("label") == "native"]
    require(len(declared) == 1 and declared[0] == {
        "label": "native", "argv": native["argv"], "timeout_seconds": 60},
        "supervised native declared/actual command differs")
    executable, _ = bound_file(receipt.get("native_test_executable"), artifacts,
                               "supervised native executable")
    require(inputs.actual_artifact(native["argv"][0], artifacts,
                                   "supervised native argv executable") == executable,
            "supervised native argv does not name the frozen executable")
    native_sources = [row for row in receipt.get("source", [])
                      if type(row) is dict and row.get("path") ==
                      "tests/native/test_auto_contrast.cpp"]
    require(len(native_sources) == 1, "supervised native assertion source missing/duplicate")
    bound_file(native_sources[0], sources, "supervised native assertion source")
    stdout_pin, raw = bound_file(native["stdout"], artifacts, "supervised native stdout")
    require(stdout_pin == contributor["observations"],
            "supervised native observation file differs from executed stdout")
    _, stderr = bound_file(native["stderr"], artifacts, "supervised native stderr")
    require(stderr == b"", "supervised native stderr is not empty")
    try:
        lines = raw.decode("utf-8-sig").splitlines()
    except UnicodeError as error:
        raise CompositionError("supervised native stdout must be UTF8") from error
    cases = []
    for line in lines:
        if line.startswith("case "):
            match = re.fullmatch(r"case ([A-Za-z0-9_.-]+)", line)
            require(match is not None, "malformed supervised native case line")
            cases.append(match.group(1))
    require(len(cases) == len(set(cases)) == 64 and
            cases == receipt.get("native_cases") and
            report["case_identity"]["name"] in cases,
            "supervised native actual64-case membership differs")
    summaries = [line for line in lines if line.startswith("auto_contrast:")]
    require(summaries == ["auto_contrast: 1286 checks; 64 named cases; sections "
                          "literals/dimensions/records/binding/resources/controls"],
            "supervised native complete passing check summary differs")
    envelopes = []
    for line in lines:
        match = PREFIX.fullmatch(line)
        if match is not None:
            row = strict_json(match.group(1).encode("utf-8"), "supervised native envelope")
            exact(row, ("schema_version", "context", "case_id", "observations", "artifacts"),
                  "supervised native envelope")
            require(type(row["schema_version"]) is int and row["schema_version"] == 1 and
                    row["context"] == "cpp_api", "supervised native envelope header differs")
            envelopes.append(row)
    require([row["case_id"] for row in envelopes] == ["A01", "A17"] and
            encode(envelopes) == encode(receipt.get("native_observation_rows")),
            "supervised native actual envelopes differ from receipt")


def passing_report(contributor, inputs, execution, context, sources, artifacts):
    report = exact(contributor["report"], ("file", "format", "role", "case_identity"),
                   contributor["id"] + ".report")
    if report["format"] == "supervised_auto_native_v1":
        supervised_auto_native_report(contributor, context, inputs, sources, artifacts)
        return
    require(report["role"] in ("native", "sdk", "python"), "unsupported contributor report role")
    required_format = "pytest_junit_v1" if report["role"] == "python" else "ctest_log_v1"
    require(report["format"] == required_format, "contributor role/report format mismatch")
    execution.case_identity(report["case_identity"], report["format"], contributor["id"])
    parsed = execution.evidence.extract_report_cases(
        inputs.pin(report["file"], contributor["id"]), report["format"], contributor["id"])
    require(all(parsed["totals"][key] == 0 for key in ("failed", "skipped", "errors")),
            contributor["id"] + ": contributor report has nonpassing cases")
    matches = [row for row in parsed["cases"]
               if execution.same(row["identity"], report["case_identity"])]
    require(len(matches) == 1 and matches[0]["outcome"] == "passed",
            contributor["id"] + ": exact passing report case missing")


def read_envelope(contributor, context, inputs, artifacts):
    raw = inputs.pin(contributor["observations"], contributor["id"] + ".stdout")
    try:
        lines = raw.decode("utf-8-sig").splitlines()
    except UnicodeError as error:
        raise CompositionError("observation stdout must be UTF8") from error
    selected = []
    for line in lines:
        match = PREFIX.fullmatch(line)
        if match is None:
            continue
        row = strict_json(match.group(1).encode("utf-8"), contributor["id"] + ".envelope")
        exact(row, ("schema_version", "context", "case_id", "observations", "artifacts"),
              contributor["id"] + ".envelope")
        require(type(row["schema_version"]) is int and row["schema_version"] == 1,
                "observation envelope schema_version must be1")
        require(row["context"] in CONTEXTS, "observation envelope context invalid")
        text(row["case_id"], "observation envelope case_id")
        if row["context"] == context and row["case_id"] == contributor["case_id"]:
            selected.append(row)
    require(len(selected) == 1, contributor["id"] + ": missing/duplicate actual AUTO_OBSERVATION_JSON envelope")
    row = selected[0]
    require(type(row["observations"]) is dict and bool(row["observations"]),
            contributor["id"] + ": empty actual observation map")
    require(type(row["artifacts"]) is dict and set(row["artifacts"]) <= {"persisted_file"},
            "envelope artifacts supports only actual persisted_file")
    persistence = None
    if "persisted_file" in row["artifacts"]:
        persistence = inputs.actual_artifact(row["artifacts"]["persisted_file"], artifacts,
                                             contributor["id"] + ".persisted_file")
    require("persisted.file_sha256" not in row["observations"],
            "persisted SHA must be computed from actual file, not envelope assertion")
    return deepcopy(row["observations"]), persistence


def ui_files(row, directory, inputs, artifacts):
    require(type(row["files"]) is dict, "UI files object required")
    for entry in row["files"].values():
        member = text(entry["member"], "UI file member")
        require(Path(member).name == member and "/" not in member and "\\" not in member,
                "UI file member must be a basename")
        inputs.actual_artifact((directory / member).as_posix(), artifacts, "UI referenced file")


def ui_inputs(row):
    result = []
    position = None
    for frame in row["frames"]:
        for event in frame["requested_events"]:
            kind = event["type"]
            if kind == "position":
                require(event["space"] == "screen", "UI input position must be screen coordinates")
                position = deepcopy(event["point"])
            elif kind == "button":
                require(position is not None, "UI button lacks actual preceding pointer position")
                result.append({"type": "pointer", "position": deepcopy(position),
                               "button": event["name"], "down": event["down"]})
            elif kind in ("key", "keys"):
                keys = [event] if kind == "key" else event["keys"]
                result.extend({"type": "key", "key": item["name"], "down": item["down"]}
                              for item in keys)
            elif kind == "text":
                result.append({"type": "text", "text": event["value"]})
            else:
                raise CompositionError(f"unsupported concrete UI event: {kind}")
    return result


def action(row, label):
    matches = [item for item in row["auto_observations"] if item["label"] == label]
    require(len(matches) == 1, f"missing/duplicate actual UI action: {label}")
    require(matches[0]["trigger"] == "physical",
            f"UI surface assertion must derive from physical action: {label}")
    return matches[0]


def settings_action(result, scenario, actual, captured_setting):
    record = actual["command_record"]
    result[scenario + ".setting"] = captured_setting
    result[scenario + ".current"] = actual["state_after"]["settings"][SETTING]
    result[scenario + ".effective"] = record["unsigned16_bits"]
    result[scenario + ".range_low"] = record["decision"]["resolved_range"][0]
    result[scenario + ".range_high"] = record["decision"]["resolved_range"][1]
    result[scenario + ".cycle_next"] = record["cycle"]["next"]


def extract_ui(contributor, context, inputs, artifacts, reader):
    case = contributor["case_id"]
    require(case in ("U01", "U07", "U08"), "only exact U01/U07/U08 UI contributors supported")
    pin = contributor["observations"]
    row = inputs.json(pin, contributor["id"] + ".raw_ui")
    report = contributor["report"]
    require(report["role"] == "python" and
            report["case_identity"]["suite"] == "pytest" and
            report["case_identity"]["classname"] in (
                "test_auto_contrast_ui", "tests.python.test_auto_contrast_ui") and
            report["case_identity"]["name"] ==
            f"test_installed_auto_contrast[{case}-{context}]",
            "UI passing contributor must be the exact independent reader case/context")
    directory = inputs.path(pin["path"]).parent
    ui_files(row, directory, inputs, artifacts)
    reader._assert_case(row, case, context, directory)
    result, persistence = {}, None
    events = ui_inputs(row)
    if case == "U01":
        for name in ("first", "second", "third"):
            result[f"cycle.{name}.range_low"] = action(row, "cycle." + name)["command_record"]["decision"]["resolved_range"][0]
    elif case == "U07":
        opened, rendered = reader.base._state(row, "workflow.rebind_opened")
        _, changed = reader.base._state(row, "workflow.rebind_values")
        actual = action(row, "workflow.rebind")
        captured = rendered["dialog"]["values"]["unsigned16_range"]
        require(captured == opened["settings"][SETTING],
                "rebind dialog captured setting differs from actual opened preference")
        settings_action(result, "override", actual, captured)
        chosen = changed["dialog"]["values"]["unsigned16_range"]
        require(type(chosen) is str and chosen in ("auto", "8", "10", "12", "14", "15", "16"),
                "actual rebind override choice invalid")
        result["override.explicit"] = 0 if chosen == "auto" else int(chosen)
    else:
        for scenario, label in (("default", "preferences.default"),
                                ("changed", "preferences.changed12"),
                                ("frozen", "preferences.frozen12")):
            actual = action(row, label)
            captured = (row["default_preferences"]["values"][SETTING] if scenario == "default"
                        else actual["state_before"]["settings"][SETTING])
            settings_action(result, scenario, actual, captured)
        fresh = row["persisted_preferences"]
        actual = action(fresh, "preferences.persisted12")
        loaded = fresh["loaded_preferences"]["values"][SETTING]
        settings_action(result, "persisted", actual, loaded)
        result["persisted.reopened_setting"] = loaded
        path = directory / row["files"]["changed_preferences"]["member"]
        persistence = inputs.actual_artifact(path.as_posix(), artifacts, "actual reopened UI preferences")
        events.extend(ui_inputs(fresh))
    return result, persistence, events


def junit_report():
    suite = ET.Element("testsuite", {"name": REPORT_SUITE, "tests": "4", "failures": "0",
                                    "errors": "0", "skipped": "0"})
    for context in CONTEXTS:
        case = ET.SubElement(suite, "testcase", {"classname": REPORT_CLASS, "name": context})
        ET.SubElement(case, "system-out").text = (
            "Passed independent composition of pinned executed contributors; "
            "no new product execution or acceptance is claimed.")
    return ET.tostring(suite, encoding="utf-8", xml_declaration=True) + b"\n"


def compose(root, manifest_path, output):
    require(sys.flags.optimize == 0, "independent assertion readers require Python without -O/-OO")
    inputs = Inputs(root)
    relative = manifest_path.as_posix()
    target = inputs.path(relative)
    require(target.is_file() and target.stat().st_size <= MAX_INPUT, "missing/oversized input manifest")
    with target.open("rb") as stream:
        manifest_bytes = stream.read(MAX_INPUT + 1)
    require(len(manifest_bytes) <= MAX_INPUT, "input manifest grew beyond16MiB")
    manifest_pin = {"path": relative, "sha256": sha(manifest_bytes)}
    manifest = inputs.json(manifest_pin, "composer manifest")
    exact(manifest, ("schema_version", "run_id", "output_directory", "surface_contract",
                     "consumer_contract", "tool_sources", "contexts"), "manifest")
    require(type(manifest["schema_version"]) is int and manifest["schema_version"] == 1,
            "manifest schema_version must be1")
    text(manifest["run_id"], "manifest run_id")
    require(output.as_posix() == manifest["output_directory"], "CLI/frozen output directory differ")
    destination = inputs.path(manifest["output_directory"])
    require(not destination.exists(), "output directory must be fresh")
    require(type(manifest["contexts"]) is dict and set(manifest["contexts"]) == set(CONTEXTS),
            "exactly four contexts required")
    consumers, reader, tool_pins = load_tools(inputs, manifest["tool_sources"])
    execution = consumers.execution
    surface = inputs.json(manifest["surface_contract"], "surface contract")
    require(surface["sha256"] == execution.contract_fingerprint(surface), "surface contract digest changed")
    require(set(surface["contexts"]) == set(CONTEXTS), "surface contract must retain four contexts")
    contract = inputs.json(manifest["consumer_contract"], "consumer contract")
    require(contract["sha256"] == consumers.contract_fingerprint(contract), "consumer contract digest changed")
    require(set(contract["contexts"]) == set(CONTEXTS), "consumer contract must retain four contexts")
    prepared, receipts = {}, {}
    for context in CONTEXTS:
        entry = exact(manifest["contexts"][context], ("producer", "contributors"), context)
        producer = entry["producer"]
        execution.producer_identity(producer, context)
        require(producer["context"] == context, f"{context}: producer context differs")
        sources = pinned_map(producer["sources"], inputs, context + ".sources")
        artifacts = pinned_map(producer["artifacts"], inputs, context + ".artifacts")
        require(all(path in sources and sources[path] == pin for path, pin in tool_pins.items()),
                f"{context}: producer sources omit/differ from frozen tool sources")
        contributors = entry["contributors"]
        require(type(contributors) is list and 1 <= len(contributors) <= 8,
                f"{context}: requires1..8 contributors")
        ids, values, events, files, contributors_receipt = set(), {}, [], [], []
        ui_cases = []
        frozen_rules = surface["contexts"][context]
        require(len(frozen_rules) == 36 and len({v["id"] for v in frozen_rules}) == 36,
                f"{context}: exact prepared36 assertion contract required")
        for contributor in contributors:
            exact(contributor, ("id", "kind", "case_id", "report", "observations"), context + ".contributor")
            text(contributor["id"], "contributor id")
            text(contributor["case_id"], "contributor case_id")
            require(contributor["id"] not in ids, "duplicate contributor id")
            ids.add(contributor["id"])
            for pin in (contributor["report"]["file"], contributor["observations"]):
                require(pin["path"] in artifacts and artifacts[pin["path"]] == pin,
                        f"{context}: contributor input omitted from exact producer artifact set")
            passing_report(contributor, inputs, execution, context, sources, artifacts)
            if context.startswith("imgui_"):
                require(contributor["kind"] == "ui", "UI contexts cannot use API/envelope stimulus")
                observed, persisted, actual_events = extract_ui(contributor, context, inputs, artifacts, reader)
                ui_cases.append(contributor["case_id"])
                start = len(events)
                events.extend(actual_events)
            else:
                require(contributor["kind"] == "envelope", "API contexts require actual stdout envelopes")
                observed, persisted = read_envelope(contributor, context, inputs, artifacts)
                actual_events, start = [], 0
            require(not (values.keys() & observed.keys()), f"{context}: duplicate/ambiguous observation IDs")
            values.update(observed)
            if persisted is not None:
                files.append(persisted)
            contributors_receipt.append({
                **deepcopy(contributor), "observed_assertion_ids": sorted(observed),
                "actual_persisted_file": persisted,
                "input_event_segment": {"first": start, "count": len(actual_events)}})
        if context.startswith("imgui_"):
            require(sorted(ui_cases) == ["U01", "U07", "U08"], "UI composition requires exact U01/U07/U08 contributors")
            execution.input_events(events, context)
        require(len(files) == 1, f"{context}: exactly one actual consumed persisted file required")
        consumer_context = contract["contexts"][context]
        require(consumer_context["surface_contract_id"] == surface["id"] and
                consumer_context["surface_contract_sha256"] == surface["sha256"] and
                consumer_context["requirement_id"] == surface["requirement_id"],
                f"{context}: consumer/surface binding differs")
        require(set(consumer_context["scenarios"]) == set(SCENARIOS), "all five consumer scenarios required")
        require(files[0] == consumer_context["scenarios"]["persisted"]["persistence"],
                f"{context}: consumed persistence path/digest differs from frozen consumer contract; re-materialize explicitly")
        values["persisted.file_sha256"] = sha(inputs.pin(files[0], context + ".persisted"))
        require(set(values) == {row["id"] for row in frozen_rules},
                f"{context}: missing/extra actual assertions: missing={sorted({v['id'] for v in frozen_rules}-values.keys())}, extra={sorted(values.keys()-{v['id'] for v in frozen_rules})}")
        assertions = []
        for rule in frozen_rules:
            actual = deepcopy(rule)
            actual["observed"] = values[rule["id"]]
            execution.assertion_rule(actual, context + "." + rule["id"], observed=True)
            assertions.append(actual)
        prepared[context] = {
            "id": "case.auto_contrast." + context, "requirement_id": surface["requirement_id"],
            "context": context, "contract_id": surface["id"], "contract_sha256": surface["sha256"],
            "report_role": "python", "report_case_identity": {
                "suite": REPORT_SUITE, "classname": REPORT_CLASS, "name": context},
            "assertions": assertions, "inputs": events,
            "contributors": contributors_receipt,
        }
        receipts[context] = {"producer": deepcopy(producer), "contributors": contributors_receipt}
    # No partial passing output, generic pass marker or expected->observed fill.
    payloads = {"composed-report.xml": junit_report()}
    report_relative = manifest["output_directory"] + "/composed-report.xml"
    report_pin = {"path": report_relative, "sha256": sha(payloads["composed-report.xml"])}
    parsed = execution.evidence.extract_report_cases(payloads["composed-report.xml"], "pytest_junit_v1", "composed report")
    require(parsed["totals"]["passed"] == 4, "composed report case set differs")
    catalogue = []
    for context in CONTEXTS:
        trace = {"schema_version": 1, "run_id": manifest["run_id"],
                 "producer_id": receipts[context]["producer"]["id"], "cases": [prepared[context]]}
        name = "trace." + context + ".json"
        payloads[name] = encode(trace)
        catalogue.append({
            "id": "trace.auto_contrast." + context,
            "path": manifest["output_directory"] + "/" + name, "sha256": sha(payloads[name]),
            "trace_case_id": prepared[context]["id"],
            "producer": receipts[context]["producer"],
            "report_role": "python", "report_case_identity": prepared[context]["report_case_identity"]})
    payloads["composition.json"] = encode({
        "schema_version": 1, "state": "composed_passing_contributor_checks_no_acceptance",
        "manifest": manifest_pin, "run_id": manifest["run_id"], "report": report_pin,
        "surface_traces_schema_version": 1, "surface_traces": catalogue,
        "contributors": receipts, "unique_input_bytes": inputs.total,
        "limits": {"input_file_bytes": MAX_INPUT, "unique_input_bytes": MAX_TOTAL,
                   "contexts": 4, "contributors_per_context": 8},
        "acceptance": False, "product_execution_by_composer": False,
        "remaining": "Root must bind real accepted run/report/artifact/runtime identities and register exact surface/consumer proofs. This file makes no acceptance writes."})
    require(all(len(raw) <= MAX_INPUT for raw in payloads.values()) and
            sum(map(len, payloads.values())) <= MAX_TOTAL, "composed outputs exceed fixed bounds")
    # Recheck each exact consumed byte after independent helper reads, with no
    # new uncapped allocation or silent symlink/path substitution.
    for relative, cached in inputs.cache.items():
        path = inputs.path(relative)
        require(path.stat().st_size == len(cached), f"input changed during composition: {relative}")
        with path.open("rb") as stream:
            require(stream.read(MAX_INPUT + 1) == cached, f"input changed during composition: {relative}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.mkdir()
    for name, raw in payloads.items():
        with (destination / name).open("xb") as stream:
            stream.write(raw)
    return {"state": "composed_no_acceptance", "contexts": 4, "assertions": 144,
            "report": report_pin, "output_directory": manifest["output_directory"],
            "unique_input_bytes": inputs.total}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = compose(args.root, args.manifest, args.output)
    except (CompositionError, AssertionError, KeyError, TypeError, ValueError,
            OSError, ImportError, RecursionError) as error:
        print(f"Auto evidence composition refused: {type(error).__name__}: {error}", file=sys.stderr)
        return 2
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
