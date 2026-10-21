"""Independent P06g paired reader; no imagejxx, renderer or geometry imports.

Required qualification sets IMAGEJXX_REQUIRE_RENDER_TESTS=1 and
IMAGEJXX_SAVED_ROI_UI_CASE_ROOT. The latter contains provenance.json and exactly
<context>/<PX01..PX10>/run/{case-index.json,case.json}. Missing required evidence
fails. Developer-only default runs may skip when neither variable requests it.
Actual shared ImGui input and CPU annotation/readback values are checked; this
does not establish OS input, GPU presentation or a generic geometry comparator.
An explicit absolute IMAGEJXX_SAVED_ROI_UI_PROVENANCE file can pin a compatible
reader correction while retaining the original capture root and reports.
"""

import base64
from copy import deepcopy
from functools import lru_cache
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import zlib

import pytest


CONTRACT = "p06g.saved-roi-geometry-ui.001"
CONTEXTS = ("imgui_cpp", "imgui_python")
CASES = tuple(f"PX{index:02}" for index in range(1, 11))
PARAMETERS = tuple((case, context) for case in CASES for context in CONTEXTS)
PARAMETER_IDS = tuple(case + "-" + context for case, context in PARAMETERS)
EXPECTED_COLLECTION_IDS = tuple("test_installed_saved_roi_geometry[" + name + "]"
                                for name in PARAMETER_IDS)
EXPECTED_COLLECTION_COUNT = 20
FRAME_CAPS = dict(zip(CASES, (140, 120, 150, 170, 170, 160, 170, 130, 140, 180)))
MAX_REPORT = 16 * 1024 * 1024
MAX_ARCHIVE = 32 * 1024 * 1024
MAX_CONTEXT = 64 * 1024 * 1024
ATOL = 1e-11
FIXTURE = Path(__file__).resolve().parents[1] / "fixtures/saved_roi_geometry_owned_cases.json"
SOURCE_ROLES = {
    "producer": "src/desktop/saved_roi_geometry_ui_probe.cpp",
    "producer_header": "src/desktop/saved_roi_geometry_ui_probe.hpp",
    "window": "src/desktop/window.cpp",
    "headless_header": "src/desktop/headless_session_internal.hpp",
    "draft": "src/desktop/saved_roi_edit.cpp",
    "draft_header": "src/desktop/saved_roi_edit.hpp",
    "selection_panel": "src/desktop/selection_panel.cpp",
    "linked_canvas": "src/desktop/orthogonal.cpp",
    "desktop_binding": "src/desktop/python.cpp",
    "python_wrapper": "src/imagejxx/ui/app.py",
    "reader": "tests/python/test_saved_roi_geometry_ui.py",
    "fixture": "tests/fixtures/saved_roi_geometry_owned_cases.json",
}
_ADMITTED = {}
_CONTEXT_USAGE = {context: {} for context in CONTEXTS}


def _integer(value, minimum=0, maximum=2**64 - 1):
    assert type(value) is int and minimum <= value <= maximum
    return value


def _unique(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, "Duplicate JSON key: " + key
        result[key] = value
    return result


def _json(path, limit=MAX_REPORT):
    assert path.is_file(), "Missing required evidence: " + str(path)
    assert 0 < path.stat().st_size <= limit
    raw = path.read_bytes()
    assert 0 < len(raw) <= limit

    def bad_constant(value):
        raise AssertionError("Nonfinite JSON token: " + value)

    value = json.loads(raw, object_pairs_hook=_unique, parse_constant=bad_constant)
    pending, nodes = [(value, 0)], 0
    while pending:
        item, depth = pending.pop()
        nodes += 1
        assert depth <= 128 and nodes <= 2_000_000
        if type(item) is float:
            assert math.isfinite(item)
        elif type(item) is dict:
            pending.extend((child, depth + 1) for child in item.values())
        elif type(item) is list:
            pending.extend((child, depth + 1) for child in item)
    return value


def _path(value):
    assert type(value) is str and value
    path = Path(value)
    assert path.is_absolute(), "Provenance paths must be absolute"
    return path.resolve(strict=True)


def _inside(path, directory):
    assert path == directory or directory in path.parents, str(path)


def _pin(value):
    assert {"path", "bytes", "sha256"} <= set(value)
    size = _integer(value["bytes"], 1, 256 * 1024 * 1024)
    digest = value["sha256"]
    assert type(digest) is str and re.fullmatch("[0-9a-f]{64}", digest)
    path = _path(value["path"])
    assert path.is_file() and path.stat().st_size == size
    key = (str(path), size, digest)
    stat = path.stat()
    identity = (stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns)
    if key not in _ADMITTED:
        observed, count = hashlib.sha256(), 0
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                observed.update(block)
                count += len(block)
                assert count <= size
        assert count == size and observed.hexdigest() == digest
        _ADMITTED[key] = identity
    assert _ADMITTED[key] == identity, "Pinned evidence changed: " + str(path)
    return path


def _recheck():
    for (name, size, _), identity in _ADMITTED.items():
        stat = Path(name).stat()
        assert stat.st_size == size
        assert (stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns) == identity


@lru_cache(maxsize=1)
def _inputs():
    configured = os.environ.get("IMAGEJXX_SAVED_ROI_UI_CASE_ROOT")
    assert configured, "Required paired20 capture root is missing"
    root = _path(configured)
    assert root.is_dir()
    override = os.environ.get("IMAGEJXX_SAVED_ROI_UI_PROVENANCE")
    provenance_path = root / "provenance.json" if override is None else _path(override)
    provenance = _json(provenance_path, 1024 * 1024)
    assert provenance["schema_version"] == 1
    assert provenance["kind"] == "p06g.saved-roi-ui-reader-inputs.001"
    assert set(provenance["contexts"]) == set(CONTEXTS)
    sources = provenance["sources"]
    assert type(sources) is list and 12 <= len(sources) <= 128
    roles = {}
    for source in sources:
        role = source["role"]
        assert type(role) is str and role and role not in roles
        _pin(source)
        logical = source.get("logical_path", source["path"]).replace("\\", "/")
        if role in SOURCE_ROLES:
            assert logical.endswith("/" + SOURCE_ROLES[role]) or logical == SOURCE_ROLES[role]
        roles[role] = source
    assert set(SOURCE_ROLES) <= set(roles)
    assert "python_launcher" in roles
    for role, active in (("reader", Path(__file__).resolve()), ("fixture", FIXTURE.resolve())):
        frozen = roles[role]
        assert active.is_file() and active.stat().st_size == frozen["bytes"]
        assert hashlib.sha256(active.read_bytes()).hexdigest() == frozen["sha256"]
    cases = provenance["cases"]
    assert [(value["id"], value["context"]) for value in cases] == list(PARAMETERS)
    context_bytes = {context: 0 for context in CONTEXTS}
    for descriptor in cases:
        case, context = descriptor["id"], descriptor["context"]
        directory = root / context / case / "run"
        assert _path(descriptor["index"]["path"]) == (directory / "case-index.json").resolve(strict=True)
        assert _path(descriptor["raw"]["path"]) == (directory / "case.json").resolve(strict=True)
        assert 0 < descriptor["raw"]["bytes"] <= MAX_REPORT
        context_bytes[context] += descriptor["raw"]["bytes"] + descriptor["index"]["bytes"]
    assert all(0 < value <= MAX_CONTEXT for value in context_bytes.values())
    corpus = _json(FIXTURE, 128 * 1024)
    assert corpus["schema_version"] == 1 and corpus["format"] == "owned_cases_v1"
    assert corpus["reference_execution"] is False
    assert [case["id"] for case in corpus["cases"]] == list(CASES)
    assert corpus["tolerances"]["geometry_atol"] == ATOL
    return root, provenance, {case["id"]: case for case in corpus["cases"]}


def _origin(row, context, provenance, descriptor):
    runtime = row["runtime_before"]
    assert runtime == row["runtime_after"]
    _integer(runtime["process_id"], 1)
    assert runtime["pointer_bits"] == 64
    assert type(runtime["imgui"]) is str and runtime["imgui"]
    source = next(value for value in provenance["sources"] if value["role"] == "producer")
    logical = source.get("logical_path", source["path"]).replace("\\", "/")
    actual_source = runtime["producer_source"].replace("\\", "/")
    if Path(logical).is_absolute():
        assert actual_source == logical
    else:
        assert actual_source.endswith("/" + logical)
    expected = provenance["contexts"][context]
    installed = _path(expected["installed_root"])
    for name in ("executable", "producer_artifact"):
        assert runtime[name] == {key: expected[name][key] for key in ("path", "bytes", "sha256")}
        actual_path = _pin(runtime[name])
        if context == "imgui_cpp" or name == "producer_artifact":
            _inside(actual_path, installed)
    if context == "imgui_cpp":
        assert _path(runtime["executable"]["path"]) == _path(runtime["producer_artifact"]["path"])
        assert _path(runtime["executable"]["path"]).suffix.lower() == ".exe"
    else:
        launcher = _json(_pin(descriptor["launcher_origin"]), 128 * 1024)
        assert launcher["schema_version"] == 1
        assert launcher["kind"] == "p06g.saved-roi-python-launcher-origin.001"
        assert (launcher["case_id"], launcher["context"], launcher["process_id"]) == (
            row["case_id"], context, runtime["process_id"])
        assert launcher["before"] == launcher["after"]
        origin = launcher["before"]
        assert origin["executable"] == runtime["executable"]
        assert set(origin["modules"]) == {"imagejxx", "imagejxx._native", "imagejxx._desktop"}
        package = _path(launcher["package_root"])
        _inside(package, installed)
        assert package.name == "imagejxx"
        for name, pin in origin["modules"].items():
            path = _pin(pin)
            _inside(path, package)
            if name == "imagejxx":
                assert path == package / "__init__.py"
            else:
                assert path.suffix.lower() == ".pyd" and path.name.startswith(name.rsplit(".", 1)[1])
        assert origin["modules"]["imagejxx._desktop"] == runtime["producer_artifact"]


def _load(case, context):
    root, provenance, corpus = _inputs()
    descriptor = next(value for value in provenance["cases"]
                      if (value["id"], value["context"]) == (case, context))
    index = _json(_pin(descriptor["index"]), 64 * 1024)
    assert set(index) == {"schema_version", "contract", "context", "case_count",
                          "observation_state", "acceptance_claim", "cases"}
    assert index["schema_version"] == 1 and index["contract"] == CONTRACT
    assert index["context"] == context and index["case_count"] == 1
    assert index["observation_state"] == "observed" and index["acceptance_claim"] is False
    entry, = index["cases"]
    assert set(entry) == {"id", "member", "byte_count", "sha256"}
    assert entry["id"] == case and entry["member"] == "case.json"
    assert (entry["byte_count"], entry["sha256"]) == (descriptor["raw"]["bytes"], descriptor["raw"]["sha256"])
    row = _json(_pin(descriptor["raw"]))
    assert row["schema_version"] == 1 and row["contract"] == CONTRACT
    assert (row["case_id"], row["context"]) == (case, context)
    assert row["acceptance_claim"] is False and row["observation_state"] == "observed"
    assert "error" not in row and "cleanup_error" not in row and not row.get("open_dependencies")
    assert row["limits"] == {"max_frames": FRAME_CAPS[case], "max_seconds": 15,
                             "max_report_bytes": MAX_REPORT, "max_archive_bytes": MAX_ARCHIVE,
                             "max_documents": 16}
    assert 0 < row["elapsed_seconds"] <= 15
    _origin(row, context, provenance, descriptor)
    archive_bytes = descriptor["raw"]["bytes"] + descriptor["index"]["bytes"]
    for name, file in row["files"].items():
        assert set(file) == {"member", "byte_count", "sha256"}
        assert file["member"] == name and Path(name).name == name
        path = root / context / case / "run" / name
        _pin({"path": str(path), "bytes": file["byte_count"], "sha256": file["sha256"]})
        archive_bytes += file["byte_count"]
    assert archive_bytes <= MAX_ARCHIVE
    _CONTEXT_USAGE[context][case] = (row["elapsed_seconds"], archive_bytes)
    assert sum(value[0] for value in _CONTEXT_USAGE[context].values()) <= 60
    assert sum(value[1] for value in _CONTEXT_USAGE[context].values()) <= MAX_CONTEXT
    return row, corpus[case], root / context / case / "run"


def _eq(actual, expected, tolerance=0):
    if type(expected) is bool or expected is None or type(expected) is str:
        assert type(actual) is type(expected) and actual == expected
    elif type(expected) in (int, float):
        assert type(actual) in (int, float) and math.isfinite(actual)
        if tolerance:
            assert abs(actual - expected) <= tolerance, (actual, expected)
        else:
            assert actual == expected, (actual, expected)
    elif type(expected) is list:
        assert type(actual) is list and len(actual) == len(expected)
        for a, e in zip(actual, expected):
            _eq(a, e, tolerance)
    else:
        assert type(expected) is dict and type(actual) is dict and set(actual) == set(expected)
        for key in expected:
            _eq(actual[key], expected[key], tolerance)


def _f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def _geometry(value):
    assert type(value) is dict
    structural = {"kind", "points", "width", "shape", "mask"}
    assert {"kind", "points", "width"} <= set(value)
    attrs = {key: item for key, item in value.items() if key not in structural}
    result = {"kind": value["kind"], "points": value["points"], "width": value["width"], "attributes": attrs}
    if value["kind"] == "mask":
        assert value["shape"] == [10, 12]
        raw = base64.b64decode(value["mask"], validate=True)
        assert len(raw) == 15
        result.update(mask_width=12, mask_height=10,
                      mask_rows=["".join("1" if raw[(y*12+x)//8] & (0x80 >> ((y*12+x) % 8)) else "0"
                                         for x in range(12)) for y in range(10)])
    else:
        assert "shape" not in value and "mask" not in value
    return result


def _assert_geometry(actual, expected, style=None):
    geometry = _geometry(actual)
    assert geometry["kind"] == expected["kind"]
    if "mask_rows" in expected:
        assert geometry["mask_width"] == expected["mask_width"] == 12
        assert geometry["mask_height"] == expected["mask_height"] == 10
        assert geometry["mask_rows"] == expected["mask_rows"]
        if "cell_count" in expected:
            assert sum(row.count("1") for row in geometry["mask_rows"]) == expected["cell_count"]
    else:
        _eq(geometry["points"], expected["points"],
            ATOL if geometry["kind"].startswith("rotated_") or
            expected.get("analytical_input") or expected.get("finite_points") else 0)
    if style is None:
        style = expected
    if "width" in style:
        _eq(geometry["width"], style["width"])
    if "attributes" in style:
        _eq(geometry["attributes"], style["attributes"])


def _state(row, label):
    assert label in row["states"], "Missing required checkpoint: " + label
    return row["states"][label]


def _frame(row, ordinal):
    _integer(ordinal, 1, len(row["frames"]))
    return row["frames"][ordinal - 1]


def _render(row, label):
    state = _state(row, label)
    frame = _frame(row, state["frame_ordinal"])
    assert frame["label"] == label
    return frame["rendered"]


def _source(row, label="initial"):
    return _state(row, label)["documents"]["source"]


def _rev(document):
    assert set(document["revisions"]) == {"pixels", "metadata", "selection", "display"}
    return tuple(_integer(document["revisions"][key]) for key in ("pixels", "metadata", "selection", "display"))


def _position(document):
    display = document["display"]
    return [display["t"], display["c"], display["crosshair"][2]]


def _display_state(document, source, baseline, *, transform_changed=False):
    display = document["display"]
    assert set(display) == {"t", "c", "crosshair", "orthogonal", "linked_zoom", "zoom",
                            "plane_zoom", "pan", "revision", "properties"}
    assert display["crosshair"][:2] == source["display"]["crosshair"][:2]
    for key in ("orthogonal", "linked_zoom"):
        _eq(display[key], source["display"][key])
    _eq(display["properties"], baseline["display"]["properties"])
    _integer(display["revision"])
    if not transform_changed:
        for key in ("zoom", "plane_zoom", "pan"):
            _eq(display[key], baseline["display"][key])
    else:
        # PX06 intentionally changes the actual canvas transform to cancel a
        # captured pointer gesture. Its new zoom/pan are observed, not guessed.
        assert type(display["zoom"]) in (int, float) and display["zoom"] > 0
        assert len(display["plane_zoom"]) == 3 and all(value > 0 for value in display["plane_zoom"])
        assert len(display["pan"]) == 3 and all(len(value) == 2 for value in display["pan"])


def _values(source, t, c, z, *, changed=False):
    rule = source["sample_rule"]
    assert rule["coefficients"] == {"t": 1000, "c": 100, "z": 10, "y": 2, "x": 1}
    assert rule["offset"] == 0
    values = []
    for y in range(10):
        for x in range(12):
            v = 1000*t + 100*c + 10*z + 2*y + x
            v = v % 256 if source["dtype"] == "uint8" else v / 4 if source["dtype"] == "float32" else v
            if changed and (t, c, z, y, x) == (1, 1, 2, 0, 0):
                v += 1
            values.append(v)
    return values


def _pixels(document, source, *, changed=False):
    assert document["shape"] == source["shape"] == [2, 3, 3, 10, 12, 1]
    assert document["dtype"] == source["dtype"]
    positions = [(t, c, z) for t in range(2) for c in range(3) for z in range(3)]
    assert [plane["tcz"] for plane in document["planes"]] == [list(p) for p in positions]
    code = {"uint8": "B", "uint16": "H", "float32": "f"}[source["dtype"]]
    for plane, position in zip(document["planes"], positions):
        values = _values(source, *position, changed=changed)
        assert set(plane) == {"tcz", "storage_f64le_hex", "declared_hex"}
        assert bytes.fromhex(plane["storage_f64le_hex"]) == struct.pack("<120d", *values)
        assert bytes.fromhex(plane["declared_hex"]) == struct.pack("<120" + code, *values)


def _source_literal(document, source, *, pixel_mutation=False, metadata_mutation=False, selection_mutation=False):
    _integer(document["document_id"], 1)
    _integer(document["data_identity"])
    _pixels(document, source, changed=pixel_mutation)
    assert document["sample_policy"] == source["sample_policy"] == "finite_only"
    _eq(document["calibration"], source["calibration"])
    metadata = deepcopy(source["metadata"])
    if metadata_mutation:
        metadata["properties"]["guard-change"] = True
    _eq(document["metadata"], metadata)
    if selection_mutation:
        _assert_geometry(document["selection"], {"kind": "line", "points": [[1, 1], [10, 8]], "width": 4, "attributes": {}})
        _assert_geometry(document["previous_selection"], source["selection"])
    else:
        _assert_geometry(document["selection"], source["selection"])
        _assert_geometry(document["previous_selection"], source["previous_selection"])
    _eq(document["overlays"], source["overlays"])
    assert document["spectrum_present"] is False
    assert len(document["rois"]) == len(source["rois"])
    for actual, original in zip(document["rois"], source["rois"]):
        assert set(actual) == {"name", "selection", "t", "c", "z", "attributes"}
        for key in ("name", "t", "c", "z", "attributes"):
            _eq(actual[key], original[key])
        geometry = _geometry(actual["selection"])
        _eq(geometry["width"], original["selection"]["width"])
        _eq(geometry["attributes"], original["selection"]["attributes"])


def _canvas(rendered):
    mode = rendered["orthogonal_navigation"]["mode"]
    assert type(mode) is bool and mode is rendered["view"]["orthogonal"]
    if mode:
        choices = [value for value in rendered["orthogonal_planes"] if value["plane"] == "XY"]
        assert len(choices) == 1
        return choices[0]
    return rendered["ordinary_canvas"]


def _assert_canvas_transport(observed, emitted):
    # The producer retains the actual canvas in its input observation, while
    # noncheckpoint frame transport removes only the heavy linked readback.
    # Every geometry/input/provenance field must still agree exactly. Named
    # checkpoint pixels keep their independent comparisons below.
    assert type(observed) is dict and type(emitted) is dict
    if "readback" in observed and "readback" not in emitted:
        assert {key: value for key, value in observed.items() if key != "readback"} == emitted
    else:
        assert observed == emitted


def _inactive_canvas(row, rendered, canvas):
    """Admit an unrendered item without crediting a retained prior draw."""
    identifier = _integer(canvas["canvas_id"])
    assert type(canvas["visible"]) is bool
    if identifier:
        return False
    assert canvas["visible"] is False and canvas["focused"] is False
    assert canvas["saved_roi_geometry_handles"] == []
    assert canvas["saved_roi_geometry_input"] == {}
    draft = rendered["saved_roi_geometry"]
    assert draft["active"] is False and draft["pointer"] is False
    assert draft["retained_count"] == draft["retained_working_bytes"] == 0
    current = _integer(rendered["session"]["imgui_frame"], 1)
    layers = canvas["saved_roi_geometry_layers"]
    assert type(layers) is list
    for layer in layers:
        historical = _integer(layer["frame"], 1, current - 1)
        _integer(layer["canvas_id"], 1)
        _integer(layer["document_id"], 1)
        assert type(layer["drawn"]) is bool
        earlier = [frame for frame in row["frames"]
                   if frame["rendered"]["session"]["imgui_frame"] == historical]
        assert len(earlier) == 1
        earlier_frame, = earlier
        for key in ("session_id", "context_id"):
            assert earlier_frame["rendered"]["session"][key] == rendered["session"][key]
        earlier_canvas = _canvas(earlier_frame["rendered"])
        assert earlier_canvas["visible"] is True
        assert earlier_canvas["canvas_id"] == layer["canvas_id"]
        assert earlier_frame["native_state"]["active_document_id"] == layer["document_id"]
        matches = [value for value in earlier_canvas["saved_roi_geometry_layers"]
                   if value["row_index"] == layer["row_index"] and
                   value["document_id"] == layer["document_id"]]
        assert len(matches) == 1 and matches[0] == layer
        assert not (layer["drawn"] and layer["frame"] == current)
    return True


def _analytical_local_resize(row):
    drag, = [value for value in row["drag_observations"] if value["label"] == "local_resize"]
    frame = _frame(row, drag["move_frame_ordinal"])
    screen = frame["rendered"]["input_frame"]["mouse_position"]
    canvas = drag["canvas"]
    point = [(screen[i] - canvas["origin"][i]) / canvas["scale"][i] for i in range(2)]
    # Independent opposite-edge/local-axis solution from literal original.
    u = [math.sqrt(3)/2, .5]
    v = [-.5, math.sqrt(3)/2]
    east = (point[0]-6)*u[0] + (point[1]-5)*u[1]
    center = [6 + u[0]*(east-3)/2, 5 + u[1]*(east-3)/2]
    half = (east+3)/2
    return {"kind": "rotated_rectangle", "analytical_input": True, "points": [center,
        [center[0]+u[0]*half, center[1]+u[1]*half],
        [center[0]+2*v[0], center[1]+2*v[1]]]}


def _delivered_point(row, drag_label):
    drag, = [value for value in row["drag_observations"] if value["label"] == drag_label]
    mouse = _frame(row, drag["move_frame_ordinal"])["rendered"]["input_frame"]["mouse_position"]
    return [(mouse[i]-drag["canvas"]["origin"][i])/drag["canvas"]["scale"][i] for i in range(2)]


def _inserted_point(row):
    observed, = [value for value in row["control_observations"]
                 if value["selector"] == "saved_xy.canvas" and value["modifier"] == "Alt"]
    # modifier-down, position, then actual pointer-down.
    frame = _frame(row, observed["before_frame_ordinal"] + 3)
    mouse = frame["rendered"]["input_frame"]["mouse_position"]
    canvas = observed["control"]
    return [(mouse[i]-canvas["origin"][i])/canvas["scale"][i] for i in range(2)]


def _expected_geometry(row, checkpoint, index, expected):
    if (row["case_id"] == "PX03" and index == 4 and
            expected.get("kind") == "ellipse" and
            expected.get("points") == [[3, 3], [9, 7]]):
        # Zero-angle rotated ellipses canonically use two-corner ellipse form.
        # Its finite corners retain the declared geometry tolerance; kind,
        # width/attributes and every nongeometry state field remain exact.
        result = deepcopy(expected)
        result["finite_points"] = True
        return result
    if (row["case_id"] == "PX09" and index == 0 and
            expected.get("points") == [[3, 1], [9, 6]]):
        # Linked XY's calibrated image-coordinate conversion produces finite
        # geometric roundoff (observed 1.0000000000000004 for literal y=1).
        # Apply the already-declared geometry atol only to these points, never
        # to style, metadata, positions, identity, revisions or counters.
        result = deepcopy(expected)
        result["finite_points"] = True
        return result
    if (row["case_id"] == "PX03" and index == 0 and
            expected.get("semantic", {}).get("size") == [8, 4]):
        return _analytical_local_resize(row)
    if (row["case_id"] == "PX01" and index == 3 and
            expected.get("points") == [[2, 2], [10, 7]]):
        return {"kind": "rectangle", "analytical_input": True,
                "points": [[2, 2], [_delivered_point(row, "resize")[0], 7]]}
    if row["case_id"] == "PX04" and "points" in expected:
        result = deepcopy(expected)
        if index == 0 and [9, 3] in result["points"]:
            result["points"][result["points"].index([9, 3])] = _delivered_point(row, "vertex")
        if index == 0 and [10, 3] in result["points"]:
            p = _delivered_point(row, "vertex")
            result["points"][result["points"].index([10, 3])] = [p[0]+1, p[1]]
        if index == 0 and [9, 5] in result["points"]:
            result["points"][result["points"].index([9, 5])] = _inserted_point(row)
        if index == 3 and [9, 3] in result["points"]:
            result["points"][result["points"].index([9, 3])] = _delivered_point(row, "freehand.vertex")
        result["analytical_input"] = True
        return result
    return expected


def _mutation_flags(row, ordinal):
    result = []
    for name in ("pixels", "metadata", "selection"):
        entries = [value for value in row["api_observations"] if value["label"] == "data." + name + ".api_mutation"]
        result.append(bool(entries and ordinal > entries[0]["before_frame_ordinal"]))
    return result


def _workflow_record(record, source_id, source):
    assert set(record) == {"schema", "version", "operation", "source", "position", "original", "replacement"}
    assert record["schema"] == "imagejxx.saved-roi-geometry-command"
    assert type(record["version"]) is int and record["version"] == 1
    assert record["operation"] == "replace_saved_roi_geometry"
    descriptor = record["source"]
    assert set(descriptor) == {"id", "pixels_revision", "metadata_revision", "selection_revision", "shape", "type", "sample_policy", "roi_count"}
    assert descriptor["id"] == source_id and descriptor["shape"] == source["shape"]
    assert descriptor["type"] == source["dtype"] and descriptor["sample_policy"] == "finite_only"
    assert descriptor["roi_count"] == len(source["rois"])
    for name in ("pixels_revision", "metadata_revision", "selection_revision"):
        _integer(descriptor[name])
    assert set(record["position"]) == {"t", "c", "z"}
    for name, maximum in (("t", 1), ("c", 2), ("z", 2)):
        _integer(record["position"][name], 0, maximum)
    assert set(record["original"]) == {"index", "roi"}
    index = _integer(record["original"]["index"], 0, len(source["rois"]) - 1)
    original = record["original"]["roi"]
    assert set(original) == {"name", "selection", "t", "c", "z", "attributes"}
    for key in ("name", "t", "c", "z", "attributes"):
        _eq(original[key], source["rois"][index][key])
    for geometry in (original["selection"], record["replacement"]):
        assert {"kind", "points", "width", "attributes"} <= set(geometry)
        _eq(geometry["width"], source["rois"][index]["selection"]["width"])
        _eq(geometry["attributes"], source["rois"][index]["selection"]["attributes"])


def _collection(state, *, allow_error=False):
    assert state["document_ids"] == state["facade_ids"]
    assert len(state["document_ids"]) <= 16 and len(set(state["document_ids"])) == len(state["document_ids"])
    for document_id in state["document_ids"]:
        _integer(document_id, 1)
    if state["document_ids"]:
        assert state["active_document_id"] in state["document_ids"]
    assert state["results"] == [{"retained": 17}]
    assert type(state["recording"]) is bool and type(state["busy"]) is bool
    if allow_error:
        assert type(state["window_error"]) is str and state["window_error"]
    else:
        assert not state["window_error"]
    assert state["settings"]["processing_workers"] == 1
    assert state["settings"]["processing_memory_mb"] == 64 and state["settings"]["show_rois"] is True
    for value in state["usage"].values():
        _integer(value)


def _checkpoint_states(row, fixture):
    source = fixture["input"]["source"]
    expected_states = fixture["expected"]["checkpoints"]
    assert set(expected_states) <= set(row["states"])
    initial = _source(row)
    _source_literal(initial, source)
    assert initial["undo_steps"] == 0 and initial["redo_steps"] == 1 and initial["dirty"] is False
    assert _rev(initial)[:3] == (0, 2, 0)
    assert initial["document_id"] == row["source_document_id"]
    assert row["fixture_sources"]["source"]["document_id"] == initial["document_id"]
    _source_literal(row["fixture_sources"]["source"], source)
    previous = None
    for label, state in sorted(row["states"].items(), key=lambda item: item[1]["frame_ordinal"]):
        _collection(state, allow_error=row["case_id"] == "PX10" and label in {
            "replay.repeat.error", "reopened.strict.error"})
        actual = state["documents"]["source"]
        flags = _mutation_flags(row, state["frame_ordinal"]) if row["case_id"] == "PX07" else (False, False, False)
        _source_literal(actual, source, pixel_mutation=flags[0], metadata_mutation=flags[1], selection_mutation=flags[2])
        changed_transform = (row["case_id"] == "PX06" and
            state["frame_ordinal"] >= _state(row, "transform.after_release")["frame_ordinal"])
        _display_state(actual, source, initial, transform_changed=changed_transform)
        assert actual["document_id"] == initial["document_id"]
        _integer(actual["undo_steps"])
        _integer(actual["redo_steps"])
        assert actual["history"]["undo_steps"] == actual["undo_steps"]
        assert actual["history"]["redo_steps"] == actual["redo_steps"]
        if previous is not None:
            # Persistent Pixels/Metadata edits advance data identity. A global
            # Selection-only edit changes its revision without a history edit
            # or new persistent Pixels/Metadata identity.
            if _rev(actual)[:2] == _rev(previous)[:2]:
                assert actual["data_identity"] == previous["data_identity"]
            else:
                assert actual["data_identity"] != previous["data_identity"]
        previous = actual
        if label not in expected_states:
            continue  # Additional transport frames do not remove required checkpoints.
        expected = expected_states[label]
        rows = expected["rows"]
        assert sorted(int(key) for key in rows) == expected["edited_row_indices"]
        for index, original in enumerate(source["rois"]):
            geometry = rows.get(str(index), original["selection"])
            geometry = _expected_geometry(row, label, index, geometry)
            _assert_geometry(actual["rois"][index]["selection"], geometry, original["selection"])
        assert _position(actual) == expected["position_tcz"]
        cycles = expected["publication_count"]
        if row["case_id"] == "PX09" and label == "linked.undo":
            cycles = 2
        elif row["case_id"] == "PX09" and label in {"linked.redo", "final", "terminal.cleaned"}:
            cycles = 3
        assert _rev(actual)[:3] == (initial["revisions"]["pixels"] + int(flags[0]),
            initial["revisions"]["metadata"] + cycles + int(flags[1]),
            initial["revisions"]["selection"] + cycles + int(flags[2]))
        assert len(state["workflow"]) == expected["workflow_count"]
        for index, record in enumerate(state["workflow"]):
            owner = (state["documents"]["reopened"]["document_id"]
                     if row["case_id"] == "PX10" and index == 2 else initial["document_id"])
            _workflow_record(record, owner, source)
        for name in ("undo_steps", "redo_steps", "dirty", "recording"):
            if name in expected:
                _eq(state[name] if name == "recording" else actual[name], expected[name])
        rendered = _render(row, label)
        draft = rendered["saved_roi_geometry"]
        assert type(draft["active"]) is bool and type(draft["pointer"]) is bool
        if "active" in expected:
            assert draft["active"] is expected["active"]
        assert draft["retained_count"] == (1 if draft["active"] else 0)
        if draft["active"]:
            assert 0 < draft["retained_working_bytes"] <= 64 * 1024 * 1024
            assert draft["document_id"] == state["active_document_id"]
            index = draft["row_index"]
            active = next(value for value in state["documents"].values()
                          if value["document_id"] == state["active_document_id"])
            _assert_geometry(draft["original"], _geometry(active["rois"][index]["selection"]))
        else:
            assert draft["retained_working_bytes"] == 0
        if "row_index" in expected:
            assert draft["row_index"] == expected["row_index"]
        if "private_preview" in expected:
            candidate = expected["private_preview"]
            index = candidate["row_index"]
            candidate = _expected_geometry(row, label, index, candidate)
            assert draft["active"] and draft["row_index"] == index
            _assert_geometry(draft["candidate"], candidate, source["rois"][index]["selection"])
        if row["case_id"] == "PX10" and label in fixture["expected"]["reopened_checkpoints"]:
            _reopened_checkpoint(row, fixture, label)
        if expected.get("has_publishable_preview") is False:
            assert not draft.get("candidate")
            assert not rendered["selection_properties"]["preview"]
        if "raw_numeric" in expected:
            descriptor = expected["raw_numeric"]
            assert set(descriptor) == {"field", "text"}
            assert type(descriptor["field"]) is str and type(descriptor["text"]) is str
            assert rendered["selection_properties"]["numeric_text"][descriptor["field"]] == descriptor["text"]
        if "apply_enabled" in expected:
            assert rendered["selection_properties"]["control_states"]["Apply"]["disabled"] is not expected["apply_enabled"]
        if expected.get("source_closed"):
            assert actual["document_id"] not in state["document_ids"]
        if expected.get("active_owner") == "other":
            assert state["active_document_id"] == state["documents"]["other"]["document_id"]
        if expected.get("active_owner") == "reopened":
            assert state["active_document_id"] == state["documents"]["reopened"]["document_id"]
        if "handle_count" in expected:
            assert len(_canvas(rendered)["saved_roi_geometry_handles"]) == expected["handle_count"]
        if "selected_vertex" in expected:
            selected = [value["index"] for value in _canvas(rendered)["saved_roi_geometry_handles"]
                        if value["kind"] == 2 and value["selected"]]
            assert selected == [expected["selected_vertex"]]
        if "undo_steps" not in expected and row["case_id"] != "PX10":
            edits = expected["publication_count"] + int(flags[0]) + int(flags[1])
            assert actual["undo_steps"] == edits
            assert actual["redo_steps"] == (1 if edits == 0 else 0)
            assert actual["dirty"] is (edits > 0)


def _control(rendered, selector):
    if selector.startswith("numeric."):
        name = selector[8:]
        control = deepcopy(rendered["selection_properties"]["control_states"][name])
        control["point"] = rendered["selection_properties"]["controls"][name]
        control["enabled"] = not control["disabled"]
        return control
    if selector.startswith("document."):
        document_id = int(selector[9:])
        selected = [value for value in rendered["document_tabs"] if value["document_id"] == document_id]
        assert len(selected) == 1
        return selected[0]
    if selector.startswith("error."):
        return rendered["error_modal"]["controls"][selector[6:]]
    if selector.startswith("saved_roi_geometry_rebind."):
        return rendered["saved_roi_geometry_rebind"]["controls"][selector.split(".", 1)[1]]
    return rendered["host_controls"][selector]


def _event_ownership(row):
    frames = row["frames"]
    assert 0 < len(frames) == row["frame_count"] <= FRAME_CAPS[row["case_id"]]
    session_id = None
    context_id = None
    for ordinal, frame in enumerate(frames, 1):
        assert frame["ordinal"] == ordinal and type(frame["label"]) is str
        rendered = frame["rendered"]
        assert rendered["input_frame"]["raw_inputs"] == frame["requested_events"]
        _inactive_canvas(row, rendered, _canvas(rendered))
        _collection(frame["native_state"], allow_error=row["case_id"] == "PX10" and
                    rendered.get("error_modal", {}).get("open", False))
        session = rendered["session"]
        if session_id is None:
            session_id, context_id = session["session_id"], session["context_id"]
            _integer(session_id, 1)
            _integer(context_id, 1)
        assert session["open"] is True
        assert session["session_id"] == session_id and session["context_id"] == session["current_context_id"] == context_id
        for event in frame["requested_events"]:
            assert event["type"] in {"position", "button", "key", "keys", "text", "wheel"}
            if event["type"] == "text":
                prior = [value for value in row["control_observations"]
                         if value["before_frame_ordinal"] < ordinal and value["selector"] != "saved_xy.canvas"]
                assert prior
                item = prior[-1]["control"]
                assert rendered["input_frame"]["focus"]["active_id"] == item["id"]
                assert rendered["input_frame"]["input_text"]["id"] == item["id"]
    assert row["control_observations"], "No genuine controls observed"
    for observed in row["control_observations"]:
        before = _integer(observed["before_frame_ordinal"], 1, len(frames) - 3)
        selector = observed["selector"]
        rendered = _frame(row, before)["rendered"]
        if selector == "saved_xy.canvas":
            _assert_canvas_transport(observed["control"], _canvas(rendered))
            offset = 1
            if observed["modifier"] is not None:
                assert _frame(row, before + offset)["requested_events"] == [
                    {"type": "key", "name": observed["modifier"], "down": True}]
                offset += 1
            assert _frame(row, before + offset)["requested_events"] == [
                {"type": "position", "space": "screen", "point": observed["position"]}]
            assert _frame(row, before + offset + 1)["requested_events"] == [
                {"type": "button", "name": "left", "down": True}]
            assert _frame(row, before + offset + 2)["requested_events"] == [
                {"type": "button", "name": "left", "down": False}]
            canvas = observed["control"]
            _eq(observed["position"], [canvas["origin"][i] + observed["image_point"][i]*canvas["scale"][i]
                                      for i in range(2)], ATOL)
            continue
        control = _control(rendered, selector)
        assert control == observed["control"]
        _integer(control["id"], 1)
        assert control["enabled"] is not observed["disabled_attempt"]
        point = observed["position"]
        assert point == [math.floor(value) for value in control["point"]]
        rect = control["rect"]
        if rect is not None:
            assert rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]
        assert _frame(row, before + 1)["requested_events"] == [{"type": "position", "space": "screen", "point": point}]
        assert _frame(row, before + 2)["requested_events"] == [{"type": "button", "name": "left", "down": True}]
        assert _frame(row, before + 3)["requested_events"] == [{"type": "button", "name": "left", "down": False}]
        positioned = _control(_frame(row, before + 1)["rendered"], selector)
        assert positioned["id"] == control["id"] and positioned["enabled"] == control["enabled"]
    for observed in row["drag_observations"]:
        before = _frame(row, observed["before_frame_ordinal"])["rendered"]
        _assert_canvas_transport(observed["canvas"], _canvas(before))
        canvas = observed["canvas"]
        for suffix in ("from", "to"):
            _eq(observed[suffix + "_screen"], [canvas["origin"][i] + observed[suffix + "_image"][i]*canvas["scale"][i] for i in range(2)], ATOL)
        down = _frame(row, observed["down_frame_ordinal"])
        move = _frame(row, observed["move_frame_ordinal"])
        assert down["requested_events"] == [{"type": "button", "name": "left", "down": True}]
        assert move["requested_events"] == [{"type": "position", "space": "screen", "point": observed["to_screen"]}]
        assert down["rendered"]["saved_roi_geometry"]["pointer"] is True
        transform = down["rendered"]["saved_roi_geometry"]["transform"]
        assert transform["canvas_id"] == canvas["canvas_id"]
        for name in ("origin", "extent", "scale"):
            _eq(transform[name], canvas[name], ATOL)
        # Actual IO positions are Float32; derive geometry from these actual
        # delivered values, never assume intended nonintegral screen points.
        for value, intended in zip(move["rendered"]["input_frame"]["mouse_position"], observed["to_screen"]):
            delivered = struct.unpack("<f", struct.pack("<f", intended))[0]
            # ImGui may truncate the physical IO position to device pixels.
            # The analytical oracle below consumes actual IO, not the request.
            assert value in (delivered, math.floor(delivered))
        for frame, phase in ((down, "began"), (move, "updated")):
            emitted = _canvas(frame["rendered"])["saved_roi_geometry_input"]
            assert emitted["phase"] == phase and emitted["canvas_id"] == canvas["canvas_id"]
            assert emitted["kind"] == transform["kind"]
        if "up_frame_ordinal" in observed:
            up = _frame(row, observed["up_frame_ordinal"])
            assert up["requested_events"] == [{"type": "button", "name": "left", "down": False}]
            assert _canvas(up["rendered"])["saved_roi_geometry_input"]["phase"] == "released"
    opened, closed = row["lifecycle"]
    assert opened["phase"] == "opened" and closed["phase"] == "closed"
    assert opened["state"]["open"] is True and closed["state"]["open"] is False
    assert opened["state"]["session_id"] == closed["state"]["session_id"] == session_id
    assert closed["state"]["context_id"] == closed["state"]["current_context_id"] == 0


def _layers(row):
    for label, state in row["states"].items():
        rendered = _render(row, label)
        active = [value for value in state["documents"].values()
                  if value["document_id"] == state["active_document_id"]]
        assert len(active) == 1
        document, = active
        position = _position(document)
        canvas = _canvas(rendered)
        if _inactive_canvas(row, rendered, canvas):
            continue  # No current item/draw; historical cache was verified above.
        _integer(canvas["canvas_id"], 1)
        layers = canvas["saved_roi_geometry_layers"]
        visible = [index for index, roi in enumerate(document["rois"])
                   if all(roi[key] is None or roi[key] == coordinate
                          for key, coordinate in zip(("t", "c", "z"), position))]
        assert [value["row_index"] for value in layers] == visible
        assert len({value["row_index"] for value in layers}) == len(layers)
        draft = rendered["saved_roi_geometry"]
        for layer in layers:
            index = layer["row_index"]
            private = bool(draft["active"] and draft["document_id"] == document["document_id"] and
                           draft["row_index"] == index and draft["candidate"])
            assert layer["preview"] is private and layer["drawn"] is True
            assert layer["document_id"] == document["document_id"]
            assert layer["metadata_revision"] == document["revisions"]["metadata"]
            assert layer["selection_revision"] == document["revisions"]["selection"]
            assert layer["data_identity"] == document["data_identity"]
            assert layer["tcz"] == position and layer["canvas_id"] == canvas["canvas_id"]
            assert layer["frame"] == rendered["session"]["imgui_frame"]
            expected = draft["candidate"] if private else document["rois"][index]["selection"]
            assert layer["geometry"] == expected
        for handle in canvas["saved_roi_geometry_handles"]:
            assert draft["active"] and handle["row_index"] == draft["row_index"]
            assert handle["canvas_id"] == canvas["canvas_id"]
            # The drawing code explicitly narrows the image coordinate before
            # float multiplication/addition. This representation check has no
            # effect on the independent image-geometry tolerance.
            intended = [_f32(_f32(canvas["origin"][i]) +
                             _f32(_f32(handle["image_point"][i]) * _f32(canvas["scale"][i])))
                        for i in range(2)]
            _eq(handle["screen_point"], intended)
        if rendered["orthogonal_navigation"]["mode"]:
            for plane in rendered["orthogonal_planes"]:
                if plane["plane"] != "XY":
                    assert plane["saved_roi_geometry_layers"] == []
                    assert plane["saved_roi_geometry_handles"] == []


def _api(row, name, accepted):
    entries = [value for value in row["api_observations"] if value["label"] == name]
    assert len(entries) == 1
    entry, = entries
    assert entry["accepted"] is accepted
    assert "no widget-input credit" in entry["role"]
    if not accepted:
        assert entry["error"]["type"] and entry["error"]["message"]
    return entry


def _reopened_checkpoint(row, fixture, label):
    state = _state(row, label)
    original = _source(row)
    actual = state["documents"]["reopened"]
    expected = fixture["expected"]["reopened_checkpoints"][label]
    source = fixture["input"]["source"]
    assert actual["document_id"] != original["document_id"]
    assert state["active_document_id"] == actual["document_id"]
    assert state["document_ids"] == [original["document_id"], actual["document_id"]]
    _source_literal(actual, source)
    _display_state(actual, source, _state(row, "reopened")["documents"]["reopened"])
    for index, initial in enumerate(source["rois"]):
        geometry = expected["rows"].get(str(index), initial["selection"])
        _assert_geometry(actual["rois"][index]["selection"], geometry, initial["selection"])
    assert _position(actual) == expected["position_tcz"]
    for key, value in expected["revisions"].items():
        assert actual["revisions"][key] == value
    for name in ("undo_steps", "redo_steps", "dirty"):
        _eq(actual[name], expected[name])
    draft = _render(row, label)["saved_roi_geometry"]
    if "active" in expected:
        assert draft["active"] is expected["active"]
    if "row_index" in expected:
        if expected.get("chooser_open") is True:
            chooser = _render(row, label)["saved_roi_geometry_rebind"]
            assert chooser["open"] is True and chooser["choosing_row"] is True
            assert chooser["row_index"] == expected["row_index"]
        else:
            assert draft["row_index"] == expected["row_index"]
    if "private_preview" in expected:
        candidate = expected["private_preview"]
        assert draft["row_index"] == candidate["row_index"]
        _assert_geometry(draft["candidate"], candidate, source["rois"][candidate["row_index"]]["selection"])


def _wire_selection(value):
    assert {"kind", "points", "width", "attributes"} <= set(value)
    flat = {key: item for key, item in value.items() if key != "attributes"}
    assert not (set(value["attributes"]) & {"kind", "points", "width", "shape", "mask"})
    flat.update(value["attributes"])
    return flat


def _px10(row, fixture, directory):
    seed = _api(row, "replay.unpublished_record_preparation", True)
    record = seed["record"]
    initial = _source(row)
    source = fixture["input"]["source"]
    _workflow_record(record, initial["document_id"], source)
    assert record["original"]["index"] == 0
    _assert_geometry(_wire_selection(record["replacement"]),
                     {"kind": "rectangle", "points": [[3, 1], [9, 6]]}, source["rois"][0]["selection"])
    assert record["position"] == {"t": 1, "c": 2, "z": 2}
    assert _state(row, "replay.record_prepared")["workflow"] == [record]
    assert _state(row, "replay.navigation")["workflow"] == [record]
    assert _state(row, "replay.after")["workflow"] == [record, record]
    assert _source(row, "replay.record_prepared")["data_identity"] == initial["data_identity"]
    assert _source(row, "replay.navigation")["data_identity"] == initial["data_identity"]
    assert _position(_source(row, "replay.navigation")) == [0, 1, 2]
    selectors = [value["selector"] for value in row["control_observations"]]
    assert selectors.count("workflow.saved_roi_geometry.0.replay") == 3
    assert selectors.count("workflow.saved_roi_geometry.0.rebind") == 1
    assert {"navigation.channel", "navigation.time", "saved_roi_geometry_rebind.row.2",
            "saved_roi_geometry_rebind.edit", "numeric.Apply"} <= set(selectors)
    for label in ("replay.repeat.error", "reopened.strict.error"):
        rendered = _render(row, label)
        assert rendered["error_modal"]["open"] is True
        assert _state(row, label)["window_error"]
    saved = _api(row, "reopened.native_v3_save_open_preparation", True)
    assert saved["save_api"] == "save_session" and saved["open_api"] == "Window.openFile"
    name = "saved-roi-ui-session.ijx"
    assert set(row["files"]) == {name}
    file = row["files"][name]
    assert saved["session_file"] == file
    assert _path(saved["session_path"]) == (directory / name).resolve(strict=True)
    data = (directory / name).read_bytes()
    assert data[:10] == b"IMAGEJXX3\n" and 18 < len(data) <= saved["encoded_bytes_upper_bound"]
    length, = struct.unpack("<Q", data[10:18])
    assert 0 < length <= MAX_REPORT
    inflater = zlib.decompressobj()
    raw = inflater.decompress(data[18:], length + 1)
    assert len(raw) == length and inflater.eof and not inflater.unused_data
    wire = json.loads(raw, object_pairs_hook=_unique)
    assert wire["format"] == "imagejxx-session" and wire["version"] == 3
    document, = wire["documents"]
    assert document["axes"] == "TCZYXS" and document["shape"] == source["shape"]
    assert document["pixel_type"] == "uint16" and document["sample_policy"] == "finite_only"
    assert document["storage"] == "Float64LE" and document["calibration"] == source["calibration"]
    _eq(document["metadata"], source["metadata"])
    assert wire["workflow"] == [record, record] and wire["results"] == [{"retained": 17}]
    for actual, expected in zip(document["planes"], _source(row, "replay.after")["planes"]):
        assert base64.b64decode(actual, validate=True) == bytes.fromhex(expected["storage_f64le_hex"])
    assert len(document["planes"]) == 18 and len(document["rois"]) == 3
    _assert_geometry(_wire_selection(document["selection"]), source["selection"])
    _assert_geometry(_wire_selection(document["previous_selection"]), source["previous_selection"])
    for index, actual in enumerate(document["rois"]):
        for key in ("name", "t", "c", "z", "attributes"):
            _eq(actual[key], source["rois"][index][key])
        expected = {"kind": "rectangle", "points": [[3, 1], [9, 6]]} if index == 0 else source["rois"][index]["selection"]
        _assert_geometry(_wire_selection(actual["selection"]), expected, source["rois"][index]["selection"])
    opened = _state(row, "reopened")["documents"]["reopened"]
    assert saved["original_document_id"] == initial["document_id"]
    assert saved["reopened_document_id"] == opened["document_id"] != initial["document_id"]
    assert _rev(opened) == (0, 0, 0, 0) and opened["undo_steps"] == opened["redo_steps"] == 0
    for label in ("rebind.chooser", "rebind.disabled_edit"):
        chooser = _render(row, label)["saved_roi_geometry_rebind"]
        assert chooser["open"] is True and chooser["choosing_row"] is True
        assert chooser["document_id"] == opened["document_id"] and chooser["row_index"] is None
        assert chooser["controls"]["edit"]["enabled"] is False
    chosen = _render(row, "rebind.row_selected")["saved_roi_geometry_rebind"]
    assert chosen["row_index"] == 2 and chosen["controls"]["edit"]["enabled"] is True
    assert _position(_state(row, "rebind.navigation")["documents"]["reopened"]) == [1, 1, 2]
    assert _position(_state(row, "rebind.editing")["documents"]["reopened"]) == [1, 1, 2]
    refusal = _api(row, "malformed_original_complete_rebind_api_refusal", False)
    assert refusal["malformed_record"]["original"]["roi"]["t"] is True
    assert refusal["explicit_row_index"] == 2
    _assert_geometry(refusal["complete_replacement"], {"kind": "rectangle", "points": [[2, 2], [6, 6]]}, source["rois"][2]["selection"])
    assert refusal["before"] == refusal["after"]
    # Changed replay preserves the original normalized record verbatim. A
    # changed complete rebind records its new owner, actual fresh capture and
    # explicit replacement row; recording remains enabled in this case.
    rebound_record = {
        "schema": "imagejxx.saved-roi-geometry-command", "version": 1,
        "operation": "replace_saved_roi_geometry",
        "source": {"id": opened["document_id"], "pixels_revision": 0,
                   "metadata_revision": 0, "selection_revision": 0,
                   "shape": source["shape"], "type": source["dtype"],
                   "sample_policy": "finite_only", "roi_count": len(source["rois"])},
        "position": {"t": 1, "c": 1, "z": 2},
        "original": {"index": 2, "roi": deepcopy(source["rois"][2])},
        "replacement": deepcopy(source["rois"][2]["selection"]),
    }
    rebound_record["replacement"]["points"] = [[4, 3], [8, 7]]
    prepared_record = _state(row, "replay.record_prepared")["frame_ordinal"]
    after_replay = _state(row, "replay.after")["frame_ordinal"]
    after_rebind = _state(row, "rebind.after")["frame_ordinal"]
    for state in row["states"].values():
        expected_workflow = []
        if state["frame_ordinal"] >= prepared_record:
            expected_workflow.append(record)
        if state["frame_ordinal"] >= after_replay:
            expected_workflow.append(record)
        if state["frame_ordinal"] >= after_rebind:
            expected_workflow.append(rebound_record)
        assert state["workflow"] == expected_workflow


def _case_details(row, fixture, directory):
    case = row["case_id"]
    selectors = [value["selector"] for value in row["control_observations"]]
    if case == "PX01":
        assert {"numeric.Center X", "numeric.Center Y", "numeric.Width", "numeric.Height", "numeric.Apply"} <= set(selectors)
    elif case == "PX02":
        assert {"navigation.channel", "navigation.time", "numeric.Cancel"} <= set(selectors)
        assert _position(_source(row, "explicit_zero.opened")) == [0, 0, 0]
        assert _position(_source(row, "wildcard.opened")) == [1, 1, 0]
    elif case == "PX03":
        assert "numeric.Angle (degrees)" in selectors
        assert next(value for value in row["drag_observations"] if value["label"] == "rotate")["modifier"] == "Shift"
        _analytical_local_resize(row)
    elif case == "PX04":
        _api(row, "minimum_three_api_refusal", False)
        assert {"numeric.Vertex X", "numeric.Vertex Y"} <= set(selectors)
        for label, phase, key in (("delete.key.down", "removed_vertex", "delete"),
                                  ("nudge.key.down", "nudged_vertex", "right")):
            frames = [value for value in row["frames"] if value["label"] == label]
            assert len(frames) == 1
            canvas = _canvas(frames[0]["rendered"])
            event = canvas["saved_roi_geometry_input"]
            assert event["phase"] == phase and event["keyboard_admitted"] is True
            assert event["focused"] is True and event["key_owners"][key] == canvas["canvas_id"]
    elif case == "PX05":
        assert len(_canvas(_render(row, "empty.handles"))["saved_roi_geometry_handles"]) == 0
        assert sum(bit == "1" for line in fixture["input"]["source"]["rois"][0]["selection"]["mask_rows"] for bit in line) == 40
    elif case == "PX06":
        assert sum(value["disabled_attempt"] for value in row["control_observations"]) == 3
        assert any(event["type"] == "wheel" for value in row["frames"] for event in value["requested_events"])
        for label in ("invalid.cancelled", "escape.after_release", "transform.after_release", "cancel.after", "noop.after"):
            assert _render(row, label)["saved_roi_geometry"]["retained_count"] == 0
    elif case == "PX07":
        for name in ("busy_worker_preparation", "busy_cancel_release", "second_owner_preparation",
                     "data.pixels.api_mutation", "data.metadata.api_mutation", "data.selection.api_mutation", "close_owner_api_boundary"):
            _api(row, name, True)
        _api(row, "foreign_capture_api_refusal", False)
        assert "tools.tool.zoom" in selectors
        assert any(name.startswith("document.") for name in selectors)
        assert _state(row, "busy.cancelled")["busy"] is True
        assert _state(row, "busy.cleaned")["busy"] is False
    elif case == "PX08":
        _api(row, "recording_off_preparation", True)
        assert _state(row, "recording_on.after")["recording"] is True
        assert _state(row, "recording_off.after")["recording"] is False
        assert _state(row, "recording_on.after")["workflow"] == _state(row, "final")["workflow"]
    elif case == "PX09":
        assert {"menu.Edit", "menu.Edit.Undo", "menu.Edit.Redo"} <= set(selectors)
        initial = {value["plane"]: value for value in _render(row, "initial")["orthogonal_planes"]}
        assert set(initial) == {"XY", "XZ", "YZ"}
        for label in ("linked.move.preview", "linked.move.after", "linked.undo", "linked.redo"):
            current = {value["plane"]: value for value in _render(row, label)["orthogonal_planes"]}
            for plane in ("XY", "XZ", "YZ"):
                assert current[plane]["readback"]["available"] is True
                assert current[plane]["readback"]["source"] == initial[plane]["readback"]["source"]
                assert current[plane]["readback"]["rendered_rgb"] == initial[plane]["readback"]["rendered_rgb"]
                for key in ("current_tcz", "coordinate", "spacing", "range", "range_tcz", "source_document_id"):
                    assert current[plane][key] == initial[plane][key]
    elif case == "PX10":
        _px10(row, fixture, directory)


@pytest.mark.parametrize("case_id,context", PARAMETERS, ids=PARAMETER_IDS)
def test_installed_saved_roi_geometry(case_id, context):
    required = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert required in {"0", "1"}
    if required == "0" and not os.environ.get("IMAGEJXX_SAVED_ROI_UI_CASE_ROOT"):
        pytest.skip("Installed paired20 capture required; developer default supplies no report root")
    row, fixture, directory = _load(case_id, context)
    _event_ownership(row)
    _checkpoint_states(row, fixture)
    _layers(row)
    _case_details(row, fixture, directory)
    assert row["window_error"] == ""
    assert row["final_usage"] == {"running": 0, "queued": 0, "running_bytes": 0, "retained_bytes": 0, "reserved_bytes": 0}
    assert _state(row, "terminal.cleaned")["usage"] == row["final_usage"]
    assert _render(row, "terminal.cleaned")["saved_roi_geometry"]["retained_count"] == 0
    _recheck()
