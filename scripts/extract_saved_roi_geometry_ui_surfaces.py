"""Extract actual saved-ROI UI values without importing any product or test.

The original paired reader is verified, never rerun. The separate supplement's
core API predicates and Numeric interaction retain their distinct scopes.
"""

import argparse
import base64
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parent.parent
CONTEXTS = ("imgui_cpp", "imgui_python")
CASES = tuple(f"PX{i:02}" for i in range(1, 11))
PARAMETERS = tuple((case, context) for case in CASES for context in CONTEXTS)
TEST_IDS = [f"test_installed_saved_roi_geometry[{case}-{context}]" for case, context in PARAMETERS]
CONTRACT = "p06g.saved-roi-geometry-ui.001"
SUPPLEMENT = "p06g.saved-roi-geometry-ui-dependencies.001"
FRAME_CAPS = dict(zip(CASES, (140, 120, 150, 170, 170, 160, 170, 130, 140, 180)))
MIB = 1024 * 1024
SOURCE_ROLES = {
    "producer": "src/desktop/saved_roi_geometry_ui_probe.cpp",
    "producer_header": "src/desktop/saved_roi_geometry_ui_probe.hpp",
    "window": "src/desktop/window.cpp", "headless_header": "src/desktop/headless_session_internal.hpp",
    "draft": "src/desktop/saved_roi_edit.cpp", "draft_header": "src/desktop/saved_roi_edit.hpp",
    "selection_panel": "src/desktop/selection_panel.cpp", "linked_canvas": "src/desktop/orthogonal.cpp",
    "desktop_binding": "src/desktop/python.cpp", "python_wrapper": "src/imagejxx/ui/app.py",
    "reader": "tests/python/test_saved_roi_geometry_ui.py",
    "fixture": "tests/fixtures/saved_roi_geometry_owned_cases.json",
    "python_launcher": "scripts/capture_saved_roi_geometry_ui.py",
}
SUPPLEMENT_IDS = {"saved_roi." + suffix for suffix in (
    "pre_cancel.callback_count", "malformed.callback_count", "control.captured_values_preserved",
    "constraint_rejection.publication_delta", "entry.hidden_fresh_refused", "target.provider_reads",
    "provider.read_plane_calls", "provider.read_region_calls", "numeric.fresh_camera_preserved")}
SCENARIOS = ("provider_metadata_publication", "valid_precancel_resolve_and_prepare",
             "malformed_request_and_record", "caller_mutation_during_resolution",
             "retained_native_constraint", "fresh_hidden_explicit_zero_row")


def require(value, message):
    if not value:
        raise ValueError(message)


def integer(value, minimum=0, maximum=2**64 - 1):
    require(type(value) is int and minimum <= value <= maximum, "Invalid strict integer")
    return value


def number(value, minimum=0, maximum=math.inf):
    require(type(value) in (int, float) and math.isfinite(value) and minimum <= value <= maximum,
            "Invalid finite number")
    return value


def get(value, *path):
    for key in path:
        require((type(value) is dict and key in value) if type(key) is str else
                (type(value) is list and type(key) is int and 0 <= key < len(value)),
                "Missing actual field: " + repr(path))
        value = value[key]
    return value


def load_helper():
    path = Path(__file__).resolve().with_name("extract_saved_roi_geometry_surfaces.py")
    require(path.is_file() and path.stat().st_size <= 128 * 1024, "Missing bounded API extraction helper")
    before = hashlib.sha256(path.read_bytes()).hexdigest()
    spec = importlib.util.spec_from_file_location("_saved_roi_api_extraction", path)
    require(spec is not None and spec.loader is not None, "Cannot load local stdlib extraction helper")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    require(hashlib.sha256(path.read_bytes()).hexdigest() == before, "API helper changed during import")
    return module


class Inputs:
    def __init__(self, helper):
        self.api, self.pins = helper, {}

    def path(self, value):
        require(type(value) is str and value, "Missing explicit path")
        path = Path(value)
        return self.api._file(path if path.is_absolute() else ROOT / path)

    def remember(self, pin):
        old = self.pins.get(pin["path"])
        require(old is None or old == pin, "Conflicting consumed input identity")
        self.pins[pin["path"]] = pin
        require(len(self.pins) <= 256 and sum(p["bytes"] for p in self.pins.values()) <= 512 * MIB,
                "Aggregate input identity budget exceeded")
        return pin

    def pin(self, supplied, limit=128 * MIB):
        require(type(supplied) is dict and {"path", "bytes", "sha256"} <= supplied.keys(), "Missing pin fields")
        integer(supplied["bytes"], 1, limit)
        actual = self.api._pin(self.path(supplied["path"]), limit)
        require((actual["bytes"], actual["sha256"]) == (supplied["bytes"], supplied["sha256"]), "Input pin differs")
        return self.remember(actual)

    def read(self, path, limit=16 * MIB, nodes=2_000_000):
        value, pin = self.api._read(path, limit, max_nodes=nodes)
        return value, self.remember(pin)

    def read_pin(self, pin, limit=16 * MIB):
        actual = self.pin(pin, limit)
        value, reread = self.read(actual["path"], limit)
        require(actual == reread, "Input changed between pin and consumption")
        return value, actual

    def same_pin(self, a, b):
        return self.pin(a) == self.pin(b)

    def finish(self):
        for pin in self.pins.values():
            require(self.api._pin(pin["path"], max(pin["bytes"], 1)) == pin, "Consumed evidence changed")


def equal(a, b):
    return json.dumps(a, sort_keys=True, separators=(",", ":"), allow_nan=False) == json.dumps(
        b, sort_keys=True, separators=(",", ":"), allow_nan=False)


def passed(report):
    require(type(report) is dict and type(report["schema_version"]) is int and report["schema_version"] == 1 and
            report["state"] == "passed" and not report.get("failures"), "Required report did not pass")


def process(row, *, pid=None):
    require(type(row) is dict and type(row["exit_code"]) is int and row["exit_code"] == 0,
            "Required process failed")
    if "state" in row:
        require(row["state"] == "passed", "Required process is not terminal-passed")
    require(0 < number(row["elapsed_seconds"]) <= number(row["timeout_seconds"]), "Process deadline exceeded")
    require(type(row["argv"]) is list and row["argv"] and all(type(v) is str for v in row["argv"]), "Invalid process argv")
    if pid is not None:
        require(integer(row["pid"], 1) == pid, "Observed producer PID differs from actual owned process")
    cleanup_keys = ("owned_job_pids_before_cleanup", "owned_job_pids_after_cleanup")
    if any(key in row for key in cleanup_keys):
        require(all(key in row and type(row[key]) is list for key in cleanup_keys), "Incomplete owned-process cleanup observations")
        for key in cleanup_keys:
            pids = row[key]
            require(len(pids) <= 64 and all(type(value) is int and value > 0 for value in pids) and len(pids) == len(set(pids)),
                    "Invalid owned-process cleanup PID observations")
        require(row["owned_job_pids_after_cleanup"] == [], "Owned process tree was not terminal after cleanup")


def inside(path, directory):
    require(path == directory or directory in path.parents, "Origin escapes declared installed root")


def origin(inputs, row, context, expected, source, launcher=None):
    runtime = row["runtime_before"]
    require(equal(runtime, row["runtime_after"]), "Runtime origin changed")
    integer(runtime["process_id"], 1)
    require(runtime["pointer_bits"] == 64 and type(runtime["imgui"]) is str and runtime["imgui"], "Missing runtime identity")
    logical = source.get("logical_path", source["path"]).replace("\\", "/")
    require(runtime["producer_source"].replace("\\", "/").endswith("/" + logical) or
            runtime["producer_source"].replace("\\", "/") == logical, "Runtime source path differs")
    installed = Path(expected["installed_root"]).resolve(strict=True)
    for name in ("executable", "producer_artifact"):
        actual = inputs.pin(runtime[name])
        require(actual == inputs.pin(expected[name]), "Loaded runtime differs from qualified installed origin")
        if context == "imgui_cpp" or name == "producer_artifact":
            inside(Path(actual["path"]), installed)
    if context == "imgui_cpp":
        require(runtime["executable"] == runtime["producer_artifact"], "Standalone artifact is not the actual executable")
    else:
        require(launcher is not None, "Missing actual Python launcher origin")
        require(launcher["schema_version"] == 1 and launcher["kind"] == "p06g.saved-roi-python-launcher-origin.001" and
                (launcher["case_id"], launcher["context"], launcher["process_id"]) ==
                (row["case_id"], context, runtime["process_id"]), "Python launcher does not bind this capture")
        require(equal(launcher["before"], launcher["after"]), "Python imported origins changed")
        observed = launcher["before"]
        require(inputs.same_pin(observed["executable"], runtime["executable"]), "Actual Python engine differs")
        package = Path(launcher["package_root"]).resolve(strict=True)
        inside(package, installed)
        require(package.name == "imagejxx" and set(observed["modules"]) ==
                {"imagejxx", "imagejxx._native", "imagejxx._desktop"}, "Python module membership differs")
        for name, pin in observed["modules"].items():
            path = Path(inputs.pin(pin)["path"])
            inside(path, package)
            require(path == package / "__init__.py" if name == "imagejxx" else
                    path.suffix.lower() == ".pyd" and path.name.startswith(name.rsplit(".", 1)[1]), "Wrong imported module")
        require(inputs.same_pin(observed["modules"]["imagejxx._desktop"], runtime["producer_artifact"]), "Loaded _desktop differs")


def proof(inputs, case_root, provenance_path, reader_report_path):
    provenance, provenance_pin = inputs.read(provenance_path, MIB)
    report, report_pin = inputs.read(reader_report_path, MIB)
    passed(report)
    require(provenance["schema_version"] == 1 and provenance["kind"] == "p06g.saved-roi-ui-reader-inputs.001" and
            set(provenance["contexts"]) == set(CONTEXTS), "Wrong original paired provenance")
    require(inputs.same_pin(report["provenance"], provenance_pin) and report["actual_cases"] == TEST_IDS,
            "Passed reader does not bind this exact paired20")
    require(type(report["processes"]) is list and len(report["processes"]) == 1,
            "Reader report must contain exactly one actual comparison process")
    for actual in report["processes"]:
        process(actual)
    xml_pin = inputs.pin(report["results_xml"], MIB)
    xml = Path(xml_pin["path"]).read_bytes()
    require(b"<!DOCTYPE" not in xml and b"<!ENTITY" not in xml, "XML declarations are not admitted")
    tree = ET.fromstring(xml)
    cases = list(tree.iter("testcase"))
    require([case.attrib.get("name") for case in cases] == TEST_IDS and
            all(case.attrib.get("classname") == "tests.python.test_saved_roi_geometry_ui" and len(case) == 0 for case in cases),
            "Reader JUnit does not contain exactly20 successful named comparisons")
    suites = list(tree.iter("testsuite"))
    require(len(suites) == 1 and suites[0].attrib["tests"] == "20" and
            all(suites[0].attrib[key] == "0" for key in ("errors", "failures", "skipped")), "Reader JUnit totals differ")
    roles = {}
    require(type(provenance["sources"]) is list and 13 <= len(provenance["sources"]) <= 128, "Invalid original source membership")
    for source in provenance["sources"]:
        role = source["role"]
        require(type(role) is str and role not in roles, "Duplicate original source role")
        inputs.pin(source, 2 * MIB)
        if role in SOURCE_ROLES:
            logical = source.get("logical_path", source["path"]).replace("\\", "/")
            require(logical == SOURCE_ROLES[role] or logical.endswith("/" + SOURCE_ROLES[role]), "Wrong source role path")
        roles[role] = source
    require(set(SOURCE_ROLES) <= set(roles), "Missing original source roles")
    corpus, corpus_pin = inputs.read_pin(roles["fixture"], 128 * 1024)
    require(corpus["schema_version"] == 1 and corpus["format"] == "owned_cases_v1" and
            corpus["reference_execution"] is False and [r["id"] for r in corpus["cases"]] == list(CASES), "Wrong owned corpus")
    descriptors = provenance["cases"]
    require([(r["id"], r["context"]) for r in descriptors] == list(PARAMETERS), "Original capture membership differs")
    capture, capture_pin = inputs.read_pin(report["capture"], MIB)
    passed(capture)
    require(inputs.same_pin(provenance["reused_capture"]["capture_report"], capture_pin) and
            inputs.same_pin(report["capture_provenance"], provenance["reused_capture"]["original_provenance"]), "Original reuse lineage differs")
    original, _ = inputs.read_pin(report["capture_provenance"], MIB)
    require(original["cases"] == descriptors and original["contexts"] == provenance["contexts"], "Original case/origin identities changed")
    old_roles = {value["role"]: value for value in original["sources"]}
    require(len(old_roles) == len(original["sources"]) and set(old_roles) == set(roles) and
            inputs.same_pin(provenance["reused_capture"]["reader_before"], old_roles["reader"]) and
            inputs.same_pin(provenance["reused_capture"]["reader_after"], roles["reader"]), "Reused reader source lineage differs")
    for role in roles:
        if role != "reader":
            require(inputs.same_pin(old_roles[role], roles[role]), "Original consumed source was silently replaced: " + role)
    require(type(capture["processes"]) is list and len(capture["processes"]) == 20 and set(capture["usage"]) == set(CONTEXTS), "Original process/usage membership differs")
    process_times = {context: 0.0 for context in CONTEXTS}
    for (_, context), actual in zip(PARAMETERS, capture["processes"]):
        process(actual)
        require(number(actual["timeout_seconds"]) == 20, "Original case process cap differs")
        process_times[context] += actual["elapsed_seconds"]
    for context in CONTEXTS:
        require(math.isclose(process_times[context], capture["usage"][context]["process_seconds"], rel_tol=0, abs_tol=1e-9) and
                0 < process_times[context] <= 60, "Original actual process usage differs")
        require(sum(integer(d["raw"]["bytes"], 1, 16 * MIB) + integer(d["index"]["bytes"], 1, 64 * 1024)
                    for d in descriptors if d["context"] == context) <= 64 * MIB, "Original raw context byte cap exceeded before parsing")
    for descriptor in descriptors:
        directory = case_root / descriptor["context"] / descriptor["id"] / "run"
        require(inputs.path(descriptor["raw"]["path"]) == (directory / "case.json").resolve(strict=True) and
                inputs.path(descriptor["index"]["path"]) == (directory / "case-index.json").resolve(strict=True), "Case layout differs")
    return provenance, roles, capture, {"provenance": provenance_pin, "reader_report": report_pin,
                                     "capture": capture_pin, "corpus": corpus_pin, "junit": xml_pin}


def state(row, label):
    value = get(row, "states", label)
    frame = get(row, "frames", integer(value["frame_ordinal"], 1, len(row["frames"])) - 1)
    require(frame["label"] == label, "Checkpoint does not reference its actual frame")
    return value


def render(row, label):
    return row["frames"][state(row, label)["frame_ordinal"] - 1]["rendered"]


def document(row, label, owner="source"):
    return get(state(row, label), "documents", owner)


def tcz(doc):
    return [integer(doc["display"]["t"]), integer(doc["display"]["c"]), integer(doc["display"]["crosshair"][2])]


def canvas(rendered):
    mode = get(rendered, "orthogonal_navigation", "mode")
    require(type(mode) is bool and mode is get(rendered, "view", "orthogonal"), "Canvas mode differs")
    if mode:
        planes = [p for p in rendered["orthogonal_planes"] if p["plane"] == "XY"]
        require(len(planes) == 1, "Missing unique actual XY canvas")
        return planes[0]
    return rendered["ordinary_canvas"]


def common(row, context, frame_cap, contract):
    require(type(row["schema_version"]) is int and row["schema_version"] == 1 and row["contract"] == contract and
            row["context"] == context and row["acceptance_claim"] is False and row["observation_state"] == "observed" and
            "error" not in row and "cleanup_error" not in row and not row.get("open_dependencies"), "Incomplete UI capture")
    require(row["limits"] == {"max_frames": frame_cap, "max_seconds": 15, "max_report_bytes": 16 * MIB,
                              "max_archive_bytes": 32 * MIB, "max_documents": 16}, "UI capture limits differ")
    require(integer(row["frame_count"], 1, frame_cap) == len(row["frames"]) and
            0 < number(row["elapsed_seconds"]) <= 15, "UI capture exceeded its bounds")
    if contract == CONTRACT:
        require(row["window_error"] == "", "Original case retained a Window error")
    else:
        require(all(value["window_error"] == "" for value in row["states"].values()) and
                all(frame["native_state"]["window_error"] == "" for frame in row["frames"]),
                "Supplement native checkpoints retained a Window error")
    for ordinal, frame in enumerate(row["frames"], 1):
        require(integer(frame["ordinal"], 1, frame_cap) == ordinal and type(frame["requested_events"]) is list and
                frame["rendered"]["input_frame"]["raw_inputs"] == frame["requested_events"], "Actual frame/event lineage differs")
    for label in row["states"]:
        state(row, label)
    lifecycle = row["lifecycle"]
    require(len(lifecycle) == 2 and [r["phase"] for r in lifecycle] == ["opened", "closed"] and
            lifecycle[0]["state"]["open"] is True and lifecycle[1]["state"]["open"] is False and
            lifecycle[0]["state"]["session_id"] == lifecycle[1]["state"]["session_id"] and
            lifecycle[1]["state"]["context_id"] == lifecycle[1]["state"]["current_context_id"] == 0, "Context was not released")
    require(set(row["final_usage"]) == {"running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"} and
            all(type(v) is int and v == 0 for v in row["final_usage"].values()), "Terminal resources remain")
    terminal = render(row, "terminal.cleaned")["saved_roi_geometry"]
    require(terminal["active"] is False and terminal["pointer"] is False and
            integer(terminal["retained_count"]) == integer(terminal["retained_working_bytes"]) == 0, "Terminal draft remains owned")


class Values:
    def __init__(self, row, pin):
        self.row, self.pin, self.values = row, pin, {}

    def add(self, suffix, value, paths, derivation="actual field", scope="actual retained UI state/input"):
        identifier = "saved_roi." + suffix
        require(identifier not in self.values and type(value) in (bool, int, str), "Duplicate or non-scalar extraction")
        self.values[identifier] = {"encoding": "scalar", "observed": value, "source": {
            "producer_id": self.row["contract"] + "." + self.row["context"],
            "contract": self.row["contract"], "context": self.row["context"], "case_id": self.row["case_id"],
            "raw_input": self.pin, "paths": paths, "derivation": derivation, "evidence_scope": scope}}


def point_text(selection):
    require(selection["kind"] == "rectangle" and len(selection["points"]) == 2, "Expected actual rectangle points")
    return ";".join(",".join(format(number(v, -sys.float_info.max), ".17g") for v in point) for point in selection["points"])


def attributes(selection):
    return {k: v for k, v in selection.items() if k not in {"kind", "points", "shape", "mask", "width"}}


def mask_count(selection):
    require(selection["kind"] == "mask" and selection["points"] == [] and len(selection["shape"]) == 2, "Expected actual packed mask")
    height, width = (integer(v, 1, 16 * MIB) for v in selection["shape"])
    require(height * width <= 16 * MIB, "Mask exceeds geometry bounds")
    packed = base64.b64decode(selection["mask"], validate=True)
    require(len(packed) == (height * width + 7) // 8, "Packed mask length differs")
    tail = (-height * width) % 8
    require(not tail or packed[-1] & ((1 << tail) - 1) == 0, "Nonzero packed mask tail")
    return sum(value.bit_count() for value in packed)


def native_state(value):
    return {key: value[key] for key in ("documents", "document_ids", "facade_ids", "active_document_id", "workflow", "results", "recording")}


def api_row(row, label, accepted):
    entries = [v for v in row["api_observations"] if v["label"] == label]
    require(len(entries) == 1 and entries[0]["accepted"] is accepted and "no widget-input credit" in entries[0]["role"], "Missing labeled API boundary")
    if not accepted:
        require(entries[0]["error"]["type"] and entries[0]["error"]["message"], "Missing actual refusal")
    return entries[0]


def wire_selection(value):
    require(type(value["attributes"]) is dict and not (value["attributes"].keys() & {"kind", "points", "width", "shape", "mask"}), "Malformed wire attributes")
    return {**{k: v for k, v in value.items() if k != "attributes"}, **value["attributes"]}


def extract_case(row, pin):
    out, case = Values(row, pin), row["case_id"]
    d = lambda label, owner="source": document(row, label, owner)
    r = lambda label: render(row, label)
    s = lambda label: state(row, label)
    if case == "PX01":
        before, after = d("move.opened"), d("move.after")
        original, moved = before["rois"][0], after["rois"][0]
        out.add("rectangle.moved_points", point_text(moved["selection"]), ["states.move.after.documents.source.rois[0].selection.points"], "format actual points with17 significant digits")
        out.add("rectangle.numeric_points", point_text(d("numeric.after")["rois"][4]["selection"]), ["states.numeric.after.documents.source.rois[4].selection.points"])
        out.add("geometry.preserved_width", format(number(moved["selection"]["width"]), ".17g"), ["states.move.after.documents.source.rois[0].selection.width"])
        out.add("geometry.attributes_preserved", equal(attributes(original["selection"]), attributes(moved["selection"])), ["states.move.opened.documents.source.rois[0].selection", "states.move.after.documents.source.rois[0].selection"], "compare all nongeometry Selection attributes")
        out.add("entry.global_selection_changes", sum(not equal(d("initial")[key], before[key]) for key in ("selection", "previous_selection")), ["states.initial.documents.source", "states.move.opened.documents.source"], "count current/previous global-selection differences")
        out.add("target.row_attributes_preserved", equal({k: v for k, v in original.items() if k != "selection"}, {k: v for k, v in moved.items() if k != "selection"}), ["states.move.opened.documents.source.rois[0]", "states.move.after.documents.source.rois[0]"], "compare complete row fields excluding replacement Selection")
        for key in ("metadata", "selection", "pixels"):
            out.add("changed." + key + "_revision_delta", integer(after["revisions"][key]) - integer(before["revisions"][key]), [f"states.move.opened.documents.source.revisions.{key}", f"states.move.after.documents.source.revisions.{key}"], "after minus before")
        out.add("changed.undo_steps_delta", integer(after["undo_steps"]) - integer(before["undo_steps"]), ["states.move.opened.documents.source.undo_steps", "states.move.after.documents.source.undo_steps"], "after minus before")
        out.add("changed.redo_steps_after", integer(after["redo_steps"]), ["states.move.after.documents.source.redo_steps"])
    elif case == "PX02":
        current, shown = d("explicit_zero.opened"), r("explicit_zero.opened")
        out.add("position.explicit_zero_tcz", ",".join(map(str, tcz(current))), ["states.explicit_zero.opened.documents.source.display"])
        controls = [shown["host_controls"][name] for name in ("navigation.time", "navigation.channel", "image.slice")]
        require(all(integer(c["id"], 1) and c["document_id"] == current["document_id"] and c["minimum"] == 1 for c in controls), "Missing actual one-based controls")
        require([integer(c["value"], 1) - 1 for c in controls] == tcz(current) and integer(controls[2]["native_slice"]) == tcz(current)[2], "Actual one-based UI controls do not match the captured native target")
        out.add("position.explicit_zero_ui", ",".join(str(integer(c["value"], 1)) for c in controls), ["frames[explicit_zero.opened].rendered.host_controls.navigation.time.value", "frames[explicit_zero.opened].rendered.host_controls.navigation.channel.value", "frames[explicit_zero.opened].rendered.host_controls.image.slice.value"])
        out.add("position.wildcard_constraint_count", sum(d("wildcard.opened")["rois"][0][key] is not None for key in ("t", "c", "z")), ["states.wildcard.opened.documents.source.rois[0]"], "count actual non-null axis constraints")
    elif case == "PX05":
        for name, label, index in (("translated", "mask_move.after", 0), ("rotated", "mask_rotate.after", 3), ("clipped", "mask_clip.after", 4)):
            out.add("mask." + name + "_members", mask_count(d(label)["rois"][index]["selection"]), [f"states.{label}.documents.source.rois[{index}].selection.mask"], "count actual MSB-packed mask membership")
        out.add("mask.empty_handle_count", len(canvas(r("empty.handles"))["saved_roi_geometry_handles"]), ["frames[empty.handles].rendered.<actual XY canvas>.saved_roi_geometry_handles"])
    elif case == "PX06":
        before = d("invalid.opened")
        labels = ("invalid.partial.disabled_apply", "invalid.nonfinite.disabled_apply", "invalid.invalid.disabled_apply")
        out.add("invalid_draft.publication_delta", sum(abs(integer(d(label)["revisions"]["metadata"]) - integer(before["revisions"]["metadata"])) for label in labels), ["states.invalid.opened.documents.source.revisions.metadata", *[f"states.{label}.documents.source.revisions.metadata" for label in labels]], "sum absolute metadata deltas for all3 disabled Apply attempts")
        out.add("failure.native_state_unchanged", equal(native_state(s("invalid.opened")), native_state(s("invalid.cancelled"))), ["states.invalid.opened", "states.invalid.cancelled"], "compare complete native documents/membership/workflow/results/recording, excluding private UI")
        out.add("failure.metadata_revision_delta", integer(d("invalid.cancelled")["revisions"]["metadata"]) - integer(before["revisions"]["metadata"]), ["states.invalid.opened.documents.source.revisions.metadata", "states.invalid.cancelled.documents.source.revisions.metadata"], "after minus before")
        out.add("noop.undo_steps_delta", integer(d("noop.after")["undo_steps"]) - integer(d("noop.opened")["undo_steps"]), ["states.noop.opened.documents.source.undo_steps", "states.noop.after.documents.source.undo_steps"], "after minus before")
        require(type(d("cancel.after")["dirty"]) is bool and type(d("cancel.opened")["dirty"]) is bool, "Dirty flags must be booleans")
        out.add("cancel.document_dirty_changes", int(d("cancel.after")["dirty"] != d("cancel.opened")["dirty"]), ["states.cancel.opened.documents.source.dirty", "states.cancel.after.documents.source.dirty"], "count actual dirty-flag change")
        out.add("terminal.retained_working_bytes", integer(r("terminal.cleaned")["saved_roi_geometry"]["retained_working_bytes"]), ["frames[terminal.cleaned].rendered.saved_roi_geometry.retained_working_bytes"])
    elif case == "PX07":
        require(s("owner.away")["active_document_id"] != d("owner.opened")["document_id"] and s("owner.returned")["active_document_id"] == d("owner.opened")["document_id"], "Missing actual away/back owner transition")
        out.add("entry.owner_roundtrip_publication_delta", integer(d("owner.returned")["revisions"]["metadata"]) - integer(d("owner.opened")["revisions"]["metadata"]), ["states.owner.opened", "states.owner.away", "states.owner.returned"], "actual source metadata delta across away/back activation")
    elif case == "PX08":
        require(s("recording_off.opened")["recording"] is False and s("recording_off.after")["recording"] is False, "Recording was not disabled")
        out.add("recording_off.workflow_append_delta", len(s("recording_off.after")["workflow"]) - len(s("recording_off.opened")["workflow"]), ["states.recording_off.opened.workflow", "states.recording_off.after.workflow"], "after minus before length")
    elif case == "PX09":
        shown = r("linked.move.preview"); xy = canvas(shown); target = shown["saved_roi_geometry"]["row_index"]
        require(shown["orthogonal_navigation"]["mode"] is True, "Linked canvas was not active")
        layers = [v for v in xy["saved_roi_geometry_layers"] if v["row_index"] == target and v["document_id"] == d("linked.move.preview")["document_id"] and v["canvas_id"] == xy["canvas_id"] and v["frame"] == shown["session"]["imgui_frame"] and v["drawn"] is True]
        require(all(v["preview"] is True for v in layers), "Target outline is not the actual private preview")
        out.add("linked_xy.target_outline_instances", len(layers), ["frames[linked.move.preview].rendered.orthogonal_planes[XY].saved_roi_geometry_layers"], "count only target/current-owner/current-canvas/current-frame drawn outlines")
        initial = {p["plane"]: p for p in r("initial")["orthogonal_planes"]}
        preserved = True
        for label in ("linked.move.preview", "linked.move.after", "linked.undo", "linked.redo"):
            planes = {p["plane"]: p for p in r(label)["orthogonal_planes"]}
            require(set(planes) == set(initial) == {"XY", "XZ", "YZ"}, "Missing linked raw readback")
            for name in planes:
                require(planes[name]["readback"]["available"] is True, "Missing actual CPU readback")
                preserved &= equal({key: planes[name]["readback"][key] for key in ("source", "rendered_rgb")}, {key: initial[name]["readback"][key] for key in ("source", "rendered_rgb")})
        out.add("linked_xzyz.raw_samples_preserved", preserved, ["frames[initial,linked.move.preview,linked.move.after,linked.undo,linked.redo].rendered.orthogonal_planes[*].readback"], "compare actual CPU source/RGB readbacks for all3 planes; no GPU claim")
        out.add("undo_redo.pixels_revision_delta", sum(abs(integer(d(label)["revisions"]["pixels"]) - integer(d("initial")["revisions"]["pixels"])) for label in ("linked.undo", "linked.redo")), ["states.initial.documents.source.revisions.pixels", "states.linked.undo.documents.source.revisions.pixels", "states.linked.redo.documents.source.revisions.pixels"], "sum absolute Pixels revision changes through Undo/Redo")
    elif case == "PX10":
        extract_replay(out)
    return out.values


def extract_replay(out):
    row = out.row
    seed = api_row(row, "replay.unpublished_record_preparation", True)
    record = seed["record"]
    out.add("record.operation", record["operation"], ["api_observations[replay.unpublished_record_preparation].record.operation"], scope="explicit unpublished record API preparation")
    for suffix, before, after, owner in (
            ("strict_replay.metadata_revision_delta", "replay.navigation", "replay.after", "source"),
            ("strict_repeat.metadata_revision_delta", "replay.after", "replay.repeat.error", "source"),
            ("reopened_owner.strict_replay_publication_delta", "reopened", "reopened.strict.error", "reopened"),
            ("full_rebind.metadata_revision_delta", "rebind.editing", "rebind.after", "reopened")):
        out.add(suffix, integer(document(row, after, owner)["revisions"]["metadata"]) - integer(document(row, before, owner)["revisions"]["metadata"]),
                [f"states.{before}.documents.{owner}.revisions.metadata", f"states.{after}.documents.{owner}.revisions.metadata"], "after minus before")
    navigated, replayed = document(row, "replay.navigation"), document(row, "replay.after")
    out.add("strict_replay.fresh_navigation_preserved", equal(navigated["display"], replayed["display"]), ["states.replay.navigation.documents.source.display", "states.replay.after.documents.source.display"], "compare complete actual fresh Display across Replay")
    saved = api_row(row, "reopened.native_v3_save_open_preparation", True)
    require(saved["save_api"] == "save_session" and saved["open_api"] == "Window.openFile", "Save/Open route differs")
    opened = document(row, "reopened", "reopened")
    require(saved["original_document_id"] == replayed["document_id"] and
            saved["reopened_document_id"] == opened["document_id"] != replayed["document_id"], "NativeV3 did not reopen a fresh owner")
    out.add("reopen.saved_row_count_delta", len(opened["rois"]) - len(replayed["rois"]), ["states.replay.after.documents.source.rois", "states.reopened.documents.reopened.rois"], "after minus before row count", "explicit NativeV3 API Save/Open")
    for key in ("undo_steps", "redo_steps"):
        out.add("reopen." + key, integer(opened[key]), ["states.reopened.documents.reopened." + key], scope="explicit NativeV3 API Save/Open")
    out.add("reopen.native_geometry_preserved", equal(opened["rois"], replayed["rois"]) and equal(opened["selection"], replayed["selection"]) and equal(opened["previous_selection"], replayed["previous_selection"]), ["states.replay.after.documents.source", "states.reopened.documents.reopened"], "compare complete saved rows and current/previous global geometry", "explicit NativeV3 API Save/Open")
    out.add("reopen.explicit_zero_and_null_distinct", all(opened["rois"][1][key] == 0 and type(opened["rois"][1][key]) is int and opened["rois"][0][key] is None for key in ("t", "c", "z")), ["states.reopened.documents.reopened.rois[0,1]"], "compare actual explicit-zero and null positions", "explicit NativeV3 API Save/Open")
    refusal = api_row(row, "malformed_original_complete_rebind_api_refusal", False)
    require(refusal["malformed_record"]["original"]["roi"]["t"] is True and equal(refusal["before"], refusal["after"]), "Malformed-original refusal mutated state")
    before = refusal["before"]["documents"]["reopened"]
    after = refusal["after"]["documents"]["reopened"]
    out.add("malformed_original.publication_delta", integer(after["revisions"]["metadata"]) - integer(before["revisions"]["metadata"]), ["api_observations[malformed_original_complete_rebind_api_refusal].before", "api_observations[malformed_original_complete_rebind_api_refusal].after"], "after minus before metadata", "explicit malformed-original API refusal")
    chooser = render(row, "rebind.row_selected")["saved_roi_geometry_rebind"]
    target = integer(chooser["row_index"], 0, len(opened["rois"]) - 1)
    fresh, accepted = document(row, "rebind.editing", "reopened"), document(row, "rebind.after", "reopened")
    candidate = render(row, "rebind.preview")["saved_roi_geometry"]["candidate"]
    workflow = state(row, "rebind.after")["workflow"]
    require(len(workflow) == len(state(row, "rebind.editing")["workflow"]) + 1 and workflow[:2] == [record, record], "Changed Rebind did not append one normalized record")
    rebound = workflow[-1]
    original_row = dict(rebound["original"]["roi"])
    original_row["selection"] = wire_selection(original_row["selection"])
    binding = rebound["source"]
    actual = (chooser["document_id"] == fresh["document_id"] == binding["id"] and rebound["original"]["index"] == target and
              render(row, "rebind.editing")["saved_roi_geometry"]["row_index"] == target and equal(original_row, fresh["rois"][target]) and
              equal(wire_selection(rebound["replacement"]), candidate) and equal(accepted["rois"][target]["selection"], candidate) and
              equal({k: v for k, v in accepted["rois"][target].items() if k != "selection"}, {k: v for k, v in fresh["rois"][target].items() if k != "selection"}) and
              equal([v for i, v in enumerate(accepted["rois"]) if i != target], [v for i, v in enumerate(fresh["rois"]) if i != target]) and
              equal(document(row, "rebind.after"), document(row, "replay.after")) and
              [rebound["position"][key] for key in ("t", "c", "z")] == tcz(fresh) and
              binding["pixels_revision"] == fresh["revisions"]["pixels"] and binding["metadata_revision"] == fresh["revisions"]["metadata"] and
              binding["selection_revision"] == fresh["revisions"]["selection"] and binding["shape"] == fresh["shape"] and
              binding["type"] == fresh["dtype"] and binding["sample_policy"] == fresh["sample_policy"] and binding["roi_count"] == len(fresh["rois"]))
    out.add("rebind.explicit_target_preserved", actual, ["frames[rebind.row_selected].rendered.saved_roi_geometry_rebind", "states.rebind.editing.documents.reopened", "frames[rebind.preview].rendered.saved_roi_geometry.candidate", "states.rebind.after"], "compare explicit chooser row, full normalized fresh-owner request, accepted replacement and all unrelated rows/old owner")


def actual_control(rendered, selector):
    if selector.startswith("numeric."):
        name = selector[8:]
        control = dict(rendered["selection_properties"]["control_states"][name])
        control["point"] = rendered["selection_properties"]["controls"][name]
        control["enabled"] = not control["disabled"]
        return control
    return rendered["host_controls"][selector]


def numeric_input(row):
    controls = row["control_observations"]
    require([c["selector"] for c in controls] == ["roi_manager.row.0.edit_saved_area", "numeric.Center X", "numeric.Apply"], "Numeric supplement control membership differs")
    text_id = None
    for observed in controls:
        control, point = observed["control"], observed["position"]
        identifier = integer(control["id"], 1)
        require(control["enabled"] is True and observed["disabled_attempt"] is False and len(point) == 2 and
                len(control["rect"]) == 4 and all(control["rect"][i] <= number(point[i]) <= control["rect"][i + 2] for i in range(2)), "Numeric control was not genuinely admitted")
        ordinal = integer(observed["before_frame_ordinal"], 1, len(row["frames"]) - 3)
        position, down, up = row["frames"][ordinal:ordinal + 3]
        require(position["requested_events"] == [{"type": "position", "space": "screen", "point": point}] and
                down["requested_events"] == [{"type": "button", "name": "left", "down": True}] and
                up["requested_events"] == [{"type": "button", "name": "left", "down": False}], "Numeric control lacks the real position/down/up sequence")
        current = actual_control(position["rendered"], observed["selector"])
        require(current["id"] == identifier and current["enabled"] is True and current["rect"] == control["rect"], "Numeric input used stale widget coordinates")
        if observed["selector"] == "numeric.Center X":
            text_id = identifier
    texts = [f for f in row["frames"] if any(e["type"] == "text" for e in f["requested_events"])]
    require(len(texts) == 1 and texts[0]["requested_events"] == [{"type": "text", "value": "6"}], "Missing actual Numeric text edit")
    input_frame = texts[0]["rendered"]["input_frame"]
    require(input_frame["focus"]["active_id"] == input_frame["input_text"]["id"] == text_id, "Numeric text was not owned by its actual widget")
    select_all = []
    for down in (True, False):
        label = "camera.value.select_all." + ("down" if down else "up")
        selected = [f for f in row["frames"] if f["label"] == label]
        require(len(selected) == 1 and selected[0]["requested_events"] ==
                [{"type": "keys", "keys": [{"name": "Ctrl", "down": down}, {"name": "A", "down": down}]}],
                "Numeric edit lacks the actual batched select-all input")
        select_all.append(selected[0]["ordinal"])
    require(select_all[0] < select_all[1] < texts[0]["ordinal"], "Numeric select-all/text order differs")


def scenario_state(value):
    require(set(value) == {"document_id", "data_identity", "revisions", "tcz", "dirty", "undo_steps", "redo_steps", "history_limit", "history_pixel_budget", "plane_calls", "region_calls"}, "Dependency state fields differ")
    integer(value["document_id"], 1)
    if value["data_identity"] is not None:
        integer(value["data_identity"])
    require(set(value["revisions"]) == {"pixels", "metadata", "selection", "display"} and len(value["tcz"]) == 3 and type(value["dirty"]) is bool, "Dependency state types differ")
    for v in [*value["revisions"].values(), *value["tcz"], *[value[k] for k in ("undo_steps", "redo_steps", "history_limit", "history_pixel_budget", "plane_calls", "region_calls")]]:
        integer(v)


def changed_scenario(before, after):
    require(before["document_id"] == after["document_id"] and before["data_identity"] != after["data_identity"] and
            all(after["revisions"][k] == before["revisions"][k] + int(k in ("metadata", "selection")) for k in before["revisions"]) and
            before["tcz"] == after["tcz"] and after["dirty"] is True and after["undo_steps"] == before["undo_steps"] + 1 and after["redo_steps"] == 0 and
            all(before[k] == after[k] for k in ("history_limit", "history_pixel_budget", "plane_calls", "region_calls")), "Dependency changed-publication policy differs")


def extract_supplement(row, pin):
    out = Values(row, pin)
    dependencies = row["api_dependencies"]
    require(set(dependencies) == {"schema_version", "scope", "limits", "observations", "scenarios", "provider_counter_scope", "complete"} and
            type(dependencies["schema_version"]) is int and dependencies["schema_version"] == 1 and dependencies["complete"] is True and
            "no widget" in dependencies["scope"], "Wrong core dependency envelope")
    defaults = {"max_xy_pixels": 16 * MIB, "max_vertices": 65536, "max_working_bytes": 64 * MIB,
                "max_record_bytes": 16 * MIB, "metadata_depth": 128}
    require(set(dependencies["limits"]) == set(defaults), "Dependency limits differ")
    for key, maximum in defaults.items():
        integer(dependencies["limits"][key], 1, maximum)
    scenarios = dependencies["scenarios"]
    require(type(scenarios) is list and [v["id"] for v in scenarios] == list(SCENARIOS), "Dependency scenario membership differs")
    for scenario in scenarios:
        scenario_state(scenario["before"]); scenario_state(scenario["after"])
    publication, precancel, malformed, capture, constraint, hidden = scenarios
    changed_scenario(publication["before"], publication["after"])
    require(publication["publication"]["changed"] is True and publication["publication"]["document_id"] == publication["before"]["document_id"] and
            0 < integer(publication["retained_before"]) <= dependencies["limits"]["max_working_bytes"] and publication["retained_after"] == 0, "Provider publication did not release its actual prepared edit")
    for scenario in (precancel, malformed, constraint, hidden):
        require(equal(scenario["before"], scenario["after"]), "Refused dependency mutated native state")
    require(precancel["resolve_error"] and precancel["prepare_error"] and malformed["request_error"] and malformed["record_error"], "Missing actual preflight refusals")
    changed_scenario(capture["before"], capture["after"])
    captured = (capture["command_source_id"] == capture["before"]["document_id"] != capture["caller_source_id"] and
                integer(capture["command_row_index"]) == 0 and integer(capture["caller_row_index"]) == 1 and capture["command_limits"] == dependencies["limits"] and
                capture["caller_limits"]["max_working_bytes"] == 1 and integer(capture["original_checkpoint_calls"]) > 1 and
                integer(capture["original_stage_calls"]) > 0 and capture["replacement_callback_calls"] == 0)
    require(type(capture["captured_values_preserved"]) is bool and capture["captured_values_preserved"] is captured,
            "Captured-caller predicate is unsupported by actual counters/binding")
    require(constraint["error"] and integer(constraint["constraint_rejections"], 1) and constraint["native_state_unchanged"] is True and
            constraint["prepared_active"] is False and constraint["retained_working_bytes"] == 0, "Retained constraint refusal is missing")
    constraint_delta = integer(constraint["after"]["revisions"]["metadata"]) - integer(constraint["before"]["revisions"]["metadata"])
    require(type(constraint["metadata_revision_delta"]) is int and constraint["metadata_revision_delta"] == constraint_delta,
            "Constraint delta differs from actual snapshots")
    require(hidden["row_index"] == 1 and hidden["row_tcz"] == [0, 0, 0] and hidden["row_tcz"] != hidden["before"]["tcz"] and hidden["error"] and type(hidden["refused"]) is bool,
            "Hidden fresh-row discriminator is missing")
    direct = {v["id"]: v for v in row["surface_observations"]}
    require(len(direct) == len(row["surface_observations"]) == 9 and set(direct) == SUPPLEMENT_IDS and
            dependencies["observations"] == row["surface_observations"][:8], "Supplement observation membership differs")
    for observation in direct.values():
        require(set(observation) == {"id", "encoding", "value"} and observation["encoding"] == "scalar" and type(observation["value"]) in (bool, int), "Supplement scalar encoding differs")
    derived = {"pre_cancel.callback_count": integer(precancel["callback_count"]), "malformed.callback_count": integer(malformed["callback_count"]),
               "control.captured_values_preserved": captured, "constraint_rejection.publication_delta": constraint_delta,
               "entry.hidden_fresh_refused": hidden["refused"]}
    for suffix, actual in derived.items():
        require(type(direct["saved_roi." + suffix]["value"]) is type(actual) and direct["saved_roi." + suffix]["value"] == actual, "Dependency observation differs from scenario evidence")
        out.add(suffix, actual, ["api_dependencies.scenarios", "surface_observations[" + "saved_roi." + suffix + "]"], "reduce actual labeled scenario/counter/snapshot facts", "explicit shared-core API dependency; no widget input credit")
    plane = direct["saved_roi.provider.read_plane_calls"]["value"]
    region = direct["saved_roi.provider.read_region_calls"]["value"]
    total = direct["saved_roi.target.provider_reads"]["value"]
    integer(plane); integer(region); integer(total)
    require(total == plane + region and all(v["after"]["plane_calls"] <= plane and v["after"]["region_calls"] <= region for v in scenarios) and
            "all fresh scenario providers" in dependencies["provider_counter_scope"], "Provider aggregate counters differ")
    for suffix, actual in (("provider.read_plane_calls", plane), ("provider.read_region_calls", region), ("target.provider_reads", total)):
        out.add(suffix, actual, ["api_dependencies.provider_counter_scope", "api_dependencies.scenarios[*].after", "surface_observations[saved_roi." + suffix + "]"], "actual provider aggregate including the independent callback-replacement owner", "explicit throwing-provider shared-core API dependency; no widget input credit")
    numeric_input(row)
    preview, fresh, after = (document(row, label) for label in ("camera.preview", "camera.fresh_display", "camera.after"))
    candidate = render(row, "camera.preview")["saved_roi_geometry"]["candidate"]
    for label in ("camera.preview", "camera.fresh_display"):
        draft = render(row, label)["saved_roi_geometry"]
        require(draft["active"] is True and draft["pointer"] is False and draft["row_index"] == 0 and equal(draft["candidate"], candidate), "Numeric draft was discarded or retargeted")
    preparation = api_row(row, "numeric_fresh_camera_api_preparation", True)
    require(equal(preparation["before_display"], preview["display"]) and equal(preparation["fresh_display"], fresh["display"]), "Camera API boundary differs from actual checkpoints")
    require(all(equal(preview[key], fresh[key]) for key in preview if key not in ("display", "revisions")) and
            all(preview["revisions"][k] == fresh["revisions"][k] for k in ("pixels", "metadata", "selection")) and
            fresh["revisions"]["display"] > preview["revisions"]["display"] and tcz(preview) == tcz(fresh), "Camera change altered Data or captured TCZ")
    require(fresh["display"]["zoom"] == preview["display"]["zoom"] + 1 and fresh["display"]["pan"][0][0] == preview["display"]["pan"][0][0] + 3 and
            fresh["display"]["crosshair"][0] == 7 and fresh["display"]["crosshair"][1:] == preview["display"]["crosshair"][1:] and
            fresh["display"]["pan"][0][1] == preview["display"]["pan"][0][1] and equal(fresh["display"]["pan"][1:], preview["display"]["pan"][1:]) and
            all(equal(fresh["display"][key], preview["display"][key]) for key in fresh["display"] if key not in ("zoom", "pan", "crosshair", "revision")),
            "No real camera-only change occurred")
    require(all(after["revisions"][k] == fresh["revisions"][k] + int(k in ("metadata", "selection")) for k in fresh["revisions"]) and
            after["data_identity"] != fresh["data_identity"] and after["undo_steps"] == fresh["undo_steps"] + 1 and after["redo_steps"] == 0 and after["dirty"] is True and
            equal(after["rois"][0]["selection"], candidate) and equal(after["rois"][1:], fresh["rois"][1:]) and
            all(equal(after[key], fresh[key]) for key in ("selection", "previous_selection", "planes", "calibration", "metadata", "overlays", "shape", "dtype", "sample_policy", "document_id")), "Numeric Apply did not publish one contained captured replacement")
    preserved = equal(after["display"], fresh["display"])
    require(type(direct["saved_roi.numeric.fresh_camera_preserved"]["value"]) is bool and direct["saved_roi.numeric.fresh_camera_preserved"]["value"] is preserved,
            "Numeric observation differs from actual fresh camera")
    out.add("numeric.fresh_camera_preserved", preserved, ["states.camera.preview", "states.camera.fresh_display", "states.camera.after", "control_observations", "api_observations[numeric_fresh_camera_api_preparation]"], "compare actual camera-only change, retained candidate and real Numeric Apply", "actual shared ImGui Numeric draft/Apply plus explicitly labeled API camera preparation")
    return out.values


def freeze_rows(freeze):
    require(type(freeze["sources"]) is list and 1 <= len(freeze["sources"]) <= 1024, "Invalid source freeze")
    rows = {}
    for pin in freeze["sources"]:
        require(type(pin["path"]) is str and pin["path"] not in rows, "Duplicate frozen source")
        rows[pin["path"].replace("\\", "/")] = pin
    return rows


def compatibility(inputs, path, supplemental, original_capture, original_roles):
    report, pin = inputs.read(path, MIB)
    passed(report)
    require(report["acceptance_claim"] is False and inputs.same_pin(report["new_build"], supplemental["build_report"]) and
            inputs.same_pin(report["original_build"], original_capture["build_report"]), "Source compatibility is for different products")
    inputs.pin(report["checker"], 128 * 1024)
    inputs.pin(report["original_producer_preimages"], MIB)
    original, _ = inputs.read_pin(report["original_reader_freeze"], MIB)
    current, _ = inputs.read_pin(supplemental["source_freeze"], MIB)
    old_rows, new_rows = freeze_rows(original), freeze_rows(current)
    for role, source in original_roles.items():
        if role in SOURCE_ROLES:
            logical = SOURCE_ROLES[role]
            require((source["bytes"], source["sha256"]) == (old_rows[logical]["bytes"], old_rows[logical]["sha256"]),
                    "Compatibility freeze does not bind the actual retained reader/producer source: " + role)
    projection_paths = {"CMakeLists.txt", "src/desktop/main.cpp", "src/desktop/python.cpp",
                        "src/desktop/saved_roi_geometry_ui_probe.cpp", "src/desktop/saved_roi_geometry_ui_probe.hpp"}
    require(len(report["projections"]) == 5 and {r["current"]["path"].replace("\\", "/") for r in report["projections"]} == projection_paths,
            "Source compatibility projection membership differs")
    for projection in report["projections"]:
        logical = projection["current"]["path"].replace("\\", "/")
        require(projection["projected_utf8_sha256"] == projection["original_utf8_sha256"] and
                len(projection["projected_utf8_sha256"]) == 64, "Source compatibility projection differs")
        require((projection["original"]["bytes"], projection["original"]["sha256"]) ==
                (old_rows[logical]["bytes"], old_rows[logical]["sha256"]) and
                (projection["current"]["bytes"], projection["current"]["sha256"]) ==
                (new_rows[logical]["bytes"], new_rows[logical]["sha256"]), "Source projection does not bind actual frozen bytes")
        inputs.pin(projection["original"], 2 * MIB); inputs.pin(projection["current"], 2 * MIB)
    unchanged = {"src/desktop/window.cpp", "src/desktop/headless_session_internal.hpp", "src/desktop/saved_roi_edit.cpp",
                 "src/desktop/saved_roi_edit.hpp", "src/desktop/selection_panel.cpp", "src/desktop/orthogonal.cpp",
                 "src/imagejxx/ui/app.py", "tests/python/test_saved_roi_geometry_ui.py", "tests/fixtures/saved_roi_geometry_owned_cases.json"}
    require(len(report["unchanged_consumed_sources"]) == 9 and {r["path"].replace("\\", "/") for r in report["unchanged_consumed_sources"]} == unchanged,
            "Unchanged source membership differs")
    for source in report["unchanged_consumed_sources"]:
        logical = source["path"].replace("\\", "/")
        require((source["bytes"], source["sha256"]) == (old_rows[logical]["bytes"], old_rows[logical]["sha256"]) ==
                (new_rows[logical]["bytes"], new_rows[logical]["sha256"]), "Consumed old source was changed")
        inputs.pin(source, 2 * MIB)
    require(set(report["new_dependencies_local_only"]) == {"src/desktop/saved_roi_geometry_dependencies.cpp", "src/desktop/saved_roi_geometry_dependencies.hpp"}, "New dependency source scope differs")
    sources = []
    logical_roles = {**SOURCE_ROLES, "main": "src/desktop/main.cpp", "cmake": "CMakeLists.txt",
                     "dependencies": "src/desktop/saved_roi_geometry_dependencies.cpp",
                     "dependencies_header": "src/desktop/saved_roi_geometry_dependencies.hpp"}
    for role, logical in logical_roles.items():
        source = new_rows[logical]
        actual = inputs.pin(source, 2 * MIB)
        sources.append({**actual, "role": role, "logical_path": logical})
    return pin, sources


def supplement_lineage(inputs, path, original_capture, original_pins):
    report, pin = inputs.read(path, MIB)
    passed(report)
    require(report["acceptance_claim"] is False and set(report["contexts"]) == set(CONTEXTS) and
            inputs.same_pin(report["original_capture"], original_pins["capture"]), "Supplement does not bind the preserved original capture")
    require(report["limits"] == {"parent_seconds_per_context": 60, "producer_seconds_per_supplement": 15,
                                 "process_seconds_per_supplement": 20, "report_bytes": 16 * MIB,
                                 "archive_bytes_per_context": 64 * MIB}, "Supplement parent limits differ")
    build, build_pin = inputs.read_pin(report["build_report"], MIB)
    delivery, delivery_pin = inputs.read_pin(report["delivery_report"], MIB)
    passed(build); passed(delivery)
    require(inputs.same_pin(build["source_freeze"], report["source_freeze"]) and
            inputs.same_pin(delivery["origins"], report["installed_origins"]), "Supplement build/install lineage differs")
    origins, origins_pin = inputs.read_pin(report["installed_origins"], MIB)
    require(set(origins["modules"]) == {"imagejxx", "imagejxx._native", "imagejxx._desktop"}, "Installed Python module membership differs")
    modules = {name: inputs.pin(value) for name, value in origins["modules"].items()}
    standalone = inputs.pin(delivery["standalone"])
    before, before_pin = inputs.read_pin(report["launcher_origins"]["before"], 128 * 1024)
    after, after_pin = inputs.read_pin(report["launcher_origins"]["after"], 128 * 1024)
    require(equal(before, after) and set(before) == {"pid", "engine", "modules"} and set(before["modules"]) == set(modules), "Supplement launcher origins changed")
    engine = inputs.pin(before["engine"])
    for name, value in modules.items():
        require(inputs.same_pin(before["modules"][name], value), "Actual supplement imported module differs from installed origin")
    require(integer(before["pid"], 1) == report["contexts"]["imgui_python"]["process"]["pid"], "Supplement Python engine PID differs")
    installed_package = Path(modules["imagejxx"]["path"]).parent
    for value in modules.values():
        inside(Path(value["path"]), installed_package)
    expected = {
        "imgui_cpp": {"installed_root": str(Path(standalone["path"]).parent.parent), "executable": standalone, "producer_artifact": standalone},
        "imgui_python": {"installed_root": str(installed_package), "executable": engine, "producer_artifact": modules["imagejxx._desktop"]}}
    artifacts = {"imgui_cpp": [standalone], "imgui_python": list(modules.values())}
    require(type(report["processes"]) is list and len(report["processes"]) == 2 and report["processes"] == [report["contexts"][c]["process"] for c in CONTEXTS], "Supplement process membership differs")
    for context in CONTEXTS:
        combined = report["combined_usage"][context]
        require(set(combined) == {"process_seconds", "producer_seconds", "archive_bytes"} and
                0 < number(combined["process_seconds"]) <= 60 and 0 < number(combined["producer_seconds"]) <= 60 and
                0 < integer(combined["archive_bytes"]) <= 64 * MIB, "Combined original/supplement context cap exceeded")
        require(combined["process_seconds"] >= original_capture["usage"][context]["process_seconds"], "Combined process usage omitted original capture")
    return report, expected, artifacts, {"provenance": pin, "build": build_pin, "delivery": delivery_pin,
                                        "origins": origins_pin, "launcher_before": before_pin, "launcher_after": after_pin}


def input_trace(row, pin):
    return {"producer_id": row["contract"] + "." + row["context"], "case_id": row["case_id"], "raw_input": pin,
            "frames": [{"ordinal": f["ordinal"], "label": f["label"], "events": f["requested_events"]}
                       for f in row["frames"] if f["requested_events"]]}


def load_case(inputs, descriptor, context, provenance, roles):
    index, index_pin = inputs.read_pin(descriptor["index"], 64 * 1024)
    row, pin = inputs.read_pin(descriptor["raw"])
    require(set(index) == {"schema_version", "contract", "context", "case_count", "observation_state", "acceptance_claim", "cases"} and
            type(index["schema_version"]) is int and index["schema_version"] == 1 and index["contract"] == CONTRACT and
            index["context"] == context and type(index["case_count"]) is int and index["case_count"] == 1 and
            index["observation_state"] == "observed" and index["acceptance_claim"] is False and len(index["cases"]) == 1, "Case index envelope differs")
    member = index["cases"][0]
    require(set(member) == {"id", "member", "byte_count", "sha256"} and member["id"] == row["case_id"] == descriptor["id"] and
            member["member"] == "case.json" and (member["byte_count"], member["sha256"]) == (pin["bytes"], pin["sha256"]), "Case index does not bind actual raw")
    common(row, context, FRAME_CAPS[row["case_id"]], CONTRACT)
    launcher = inputs.read_pin(descriptor["launcher_origin"], 128 * 1024)[0] if context == "imgui_python" else None
    origin(inputs, row, context, provenance["contexts"][context], roles["producer"], launcher)
    archive = pin["bytes"] + index_pin["bytes"]
    for name, file in row["files"].items():
        require(set(file) == {"member", "byte_count", "sha256"} and file["member"] == name and Path(name).name == name, "Unsafe case file transport")
        actual = inputs.pin({"path": str(Path(pin["path"]).parent / name), "bytes": file["byte_count"], "sha256": file["sha256"]}, 32 * MIB)
        archive += actual["bytes"]
    require(archive <= 32 * MIB, "Case archive cap exceeded")
    return row, pin, archive, launcher


def extract(args):
    helper = load_helper()
    inputs = Inputs(helper)
    extractor_pin = inputs.pin(helper._pin(__file__, 128 * 1024))
    helper_pin = inputs.pin(helper._pin(Path(__file__).with_name("extract_saved_roi_geometry_surfaces.py"), 128 * 1024))
    contracts, commands_pin = helper._contracts(ROOT / "docs/parity/commands.json")
    inputs.remember(commands_pin)
    case_root = args.case_root.resolve(strict=True)
    require(case_root.is_dir(), "Missing original case root")
    provenance, roles, capture, original_pins = proof(inputs, case_root, args.provenance, args.reader_report)
    supplemental, expected, new_artifacts, supplemental_pins = supplement_lineage(inputs, args.supplement_provenance, capture, original_pins)
    compatibility_pin, new_sources = compatibility(inputs, args.source_compatibility, supplemental, capture, roles)
    values = {c: {} for c in CONTEXTS}
    traces = {c: [] for c in CONTEXTS}
    producers = []
    for context in CONTEXTS:
        producer_cases, artifacts = [], {}
        elapsed, archive = 0.0, 0
        for descriptor in [v for v in provenance["cases"] if v["context"] == context]:
            row, pin, byte_count, launcher = load_case(inputs, descriptor, context, provenance, roles)
            observed = extract_case(row, pin)
            require(not (values[context].keys() & observed.keys()), "Duplicate original UI extraction")
            values[context].update(observed)
            traces[context].append(input_trace(row, pin))
            producer_cases.append({"id": row["case_id"], "raw_input": pin})
            runtime_artifacts = [row["runtime_before"]["producer_artifact"]] if launcher is None else list(launcher["before"]["modules"].values())
            for artifact in runtime_artifacts:
                actual = inputs.pin(artifact)
                artifacts[actual["path"]] = actual
            elapsed += row["elapsed_seconds"]; archive += byte_count
        require(len(values[context]) == 42 and math.isclose(elapsed, capture["usage"][context]["producer_seconds"], rel_tol=0, abs_tol=1e-9) and
                archive == capture["usage"][context]["archive_bytes"] and elapsed <= 60 and archive <= 64 * MIB, "Original capture aggregate differs")
        producers.append({"id": CONTRACT + "." + context, "id_basis": "actual raw contract plus context; raw has no producer_id", "contract": CONTRACT,
                          "context": context, "sources": list(roles.values()), "artifacts": list(artifacts.values()), "cases": producer_cases})
        path = args.cpp_supplement if context == "imgui_cpp" else args.python_supplement
        row, pin = inputs.read(path)
        descriptor = supplemental["contexts"][context]
        require(inputs.same_pin(descriptor["supplement"], pin), "Supplement raw input differs from its actual capture")
        index, index_pin = inputs.read_pin(descriptor["index"], 64 * 1024)
        require(set(index) == {"schema_version", "contract", "context", "observation_state", "acceptance_claim", "member", "byte_count", "sha256"} and
                type(index["schema_version"]) is int and index["schema_version"] == 1 and index["contract"] == SUPPLEMENT and index["context"] == context and
                index["observation_state"] == "observed" and index["acceptance_claim"] is False and index["member"] == "supplement.json" and
                (index["byte_count"], index["sha256"]) == (pin["bytes"], pin["sha256"]), "Supplement index differs")
        require(row["case_id"] == "api-dependencies-and-numeric-camera", "Supplement case identity differs")
        common(row, context, 140, SUPPLEMENT)
        runtime = row["runtime_before"]
        process(descriptor["process"], pid=integer(runtime["process_id"], 1))
        if context == "imgui_cpp":
            origin(inputs, row, context, expected[context], next(s for s in new_sources if s["role"] == "producer"))
        else:
            require(inputs.same_pin(runtime["executable"], expected[context]["executable"]) and inputs.same_pin(runtime["producer_artifact"], expected[context]["producer_artifact"]) and
                    equal(runtime, row["runtime_after"]) and runtime["pointer_bits"] == 64 and type(runtime["imgui"]) is str and runtime["imgui"], "Supplement actual Python runtime differs")
            require(runtime["producer_source"].replace("\\", "/").endswith("/src/desktop/saved_roi_geometry_ui_probe.cpp"), "Supplement source identity differs")
        observed = extract_supplement(row, pin)
        require(set(observed) == SUPPLEMENT_IDS and not (values[context].keys() & observed.keys()), "Supplement ownership differs")
        values[context].update(observed)
        traces[context].append(input_trace(row, pin))
        producers.append({"id": SUPPLEMENT + "." + context, "id_basis": "actual raw contract plus context; raw has no producer_id", "contract": SUPPLEMENT,
                          "context": context, "sources": new_sources, "artifacts": new_artifacts[context], "cases": [{"id": row["case_id"], "raw_input": pin}]})
        combined = supplemental["combined_usage"][context]
        require(integer(descriptor["supplement_storage"]["bytes"]) == pin["bytes"] + index_pin["bytes"] and descriptor["supplement_storage"]["files"] == 2 and
                combined["archive_bytes"] == archive + pin["bytes"] + index_pin["bytes"] and
                math.isclose(combined["producer_seconds"], elapsed + row["elapsed_seconds"], rel_tol=0, abs_tol=1e-9) and
                math.isclose(combined["process_seconds"], capture["usage"][context]["process_seconds"] + descriptor["process"]["elapsed_seconds"], rel_tol=0, abs_tol=1e-9), "Combined actual usage does not include both origins")
    contexts = {}
    for context in CONTEXTS:
        surfaces, seen = [], set()
        for contract in contracts:
            comparisons = []
            for assertion in contract["contexts"][context]:
                require(set(assertion) in ({"id", "rule", "value_type", "expected", "observation"}, {"id", "rule", "value_type", "expected", "observation", "ui_state"}) and
                        assertion["rule"] == "scalar_equal" and ("ui_state" not in assertion or assertion["ui_state"] is True), "Unsupported frozen UI rule")
                identifier = assertion["id"]
                require(identifier not in seen and identifier in values[context], "Missing or duplicate UI assertion")
                observed = values[context][identifier]
                helper._typed(observed["observed"], assertion["value_type"], context + ":" + identifier)
                require(observed["observed"] == assertion["expected"], "Actual UI value differs: " + context + ":" + identifier)
                comparisons.append({**assertion, **observed}); seen.add(identifier)
            surfaces.append({"requirement_id": contract["requirement_id"], "contract_id": contract["id"], "contract_sha256": contract["sha256"], "assertions": comparisons})
        require(seen == set(values[context]) and len(seen) == 51, "UI51 assertion membership differs")
        contexts[context] = surfaces
    inputs.finish()
    return {"schema_version": 1, "kind": "p06g.saved-roi-ui-surface-extractions.001", "complete": True, "acceptance_claim": False,
            "inputs": {"extractor": extractor_pin, "api_extractor": helper_pin, "commands": commands_pin, "original": original_pins,
                       "supplement": supplemental_pins, "source_compatibility": compatibility_pin},
            "producers": producers, "input_traces": traces, "contexts": contexts}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("case-root", "provenance", "reader-report", "cpp-supplement", "python-supplement", "supplement-provenance", "source-compatibility", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args(argv)
    parent = args.output.parent.resolve(strict=True)
    require(parent.is_dir() and args.output.name not in ("", ".", ".."), "Fresh output path with existing parent is required")
    output = parent / args.output.name
    require(not os.path.lexists(output), "Output must be fresh")
    payload = json.dumps(extract(args), sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode("utf-8") + b"\n"
    require(len(payload) <= 4 * MIB, "UI extraction exceeds4MiB")
    with output.open("xb") as stream:
        require(stream.write(payload) == len(payload), "Incomplete UI extraction write")
        stream.flush(); os.fsync(stream.fileno())


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError, ET.ParseError) as error:
        print("saved-ROI UI extraction refused: " + str(error), file=sys.stderr)
        raise SystemExit(1) from error

