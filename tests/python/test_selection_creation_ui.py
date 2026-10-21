"""Independent P05l physical selection-creation observations in both launches.

Frozen pointer/modifier inputs and literal cells determine every expectation.
Native helpers, observed final geometry, producer pass flags and workflow
results never calculate expected candidates or Boolean masks. Setup/API rows
are identified separately and cannot establish physical gesture coverage.
"""

import base64
import copy
import hashlib
import importlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import struct
import subprocess
import sysconfig
import time
import zlib

import pytest


def _stream_sha256(stream):
    """Hash remaining binary stream bytes with bounded memory on Python 3.10+."""
    digest = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(block)
    return digest.hexdigest()


CONTRACT = "p05l.selection-creation-ui.001"
CASES = tuple(f"S{i:02d}" for i in range(1, 17))
CONTEXTS = ("imgui_cpp", "imgui_python")
EXPECTED_COLLECTION_COUNT = 32
EXPECTED_COLLECTION_IDS = tuple(f"test_installed_selection_creation[{case_id}-{context}]"
                                for case_id in CASES for context in CONTEXTS)
WIDTH, HEIGHT = 32, 24
MAX_REPORT, MAX_ARCHIVE = 16 * 1024**2, 32 * 1024**2
HEADLESS_VIEWPORT = (1250, 820)  # HeadlessInputSession's frozen actual IO size.
KEYS = {"Ctrl", "Shift", "Alt", "Super", "A", "W", "Enter", "Escape", "Tab",
        "Left", "Right", "Up", "Down", "Backspace", "Delete", "Space", "Home", "End", "PageUp", "PageDown"}


def _unique(pairs):
    value = {}
    for key, item in pairs:
        assert key not in value, f"Duplicate observation field: {key}"
        value[key] = item
    return value


def _nonfinite(value):
    raise AssertionError(f"Nonfinite observation JSON constant: {value}")


def _decode(raw, maximum=MAX_REPORT):
    raw = raw.encode("utf-8") if isinstance(raw, str) else raw
    assert isinstance(raw, bytes) and 0 < len(raw) <= maximum
    return json.loads(raw, object_pairs_hook=_unique, parse_constant=_nonfinite)


def _read(path):
    assert path.is_file() and not path.is_symlink() and 0 < path.stat().st_size <= MAX_REPORT
    return _decode(path.read_bytes())


def _integer(value, minimum=0):
    assert type(value) is int and value >= minimum
    return value


def _number(value):
    assert type(value) in (int, float) and math.isfinite(value)
    return value


def _typed(value):
    if isinstance(value, dict):
        return {key: _typed(item) for key, item in value.items()}
    if isinstance(value, list):
        return [_typed(item) for item in value]
    if type(value) is float:
        return "float64", struct.pack("<d", value).hex()
    return type(value).__name__, value


def _member(root, name):
    assert isinstance(name, str) and name and "\\" not in name and ":" not in name
    relative = PurePosixPath(name)
    assert not relative.is_absolute() and relative.as_posix() == name
    assert all(part not in {"", ".", ".."} for part in relative.parts)
    path = root.joinpath(*relative.parts)
    assert not path.is_symlink()
    path = path.resolve(strict=True)
    assert path.is_file() and path.is_relative_to(root.resolve(strict=True))
    return path


def _required():
    value = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS", "0")
    assert value in {"0", "1"}
    return value == "1"


@pytest.fixture(scope="module")
def installed_cpp_executable():
    configured = os.environ.get("IMAGEJXX_CPP_EXE")
    if not configured:
        if _required():
            pytest.fail("Set IMAGEJXX_CPP_EXE to the matching installed desktop")
        pytest.skip("Set IMAGEJXX_CPP_EXE to the installed desktop")
    path = Path(configured).resolve(strict=True)
    assert path.is_file() and path.parent.name.lower() == "bin" and path.name.lower() in {"imagejxx", "imagejxx.exe"}
    sha = os.environ.get("IMAGEJXX_CPP_EXE_SHA256")
    if _required():
        assert sha, "Required paired gate must pin IMAGEJXX_CPP_EXE_SHA256"
    if sha:
        with path.open("rb") as stream:
            assert _stream_sha256(stream) == sha
    return path


@pytest.fixture(scope="module")
def standalone_cases(installed_cpp_executable):
    configured = os.environ.get("IMAGEJXX_SELECTION_CREATION_CPP_REPORT")
    if not configured:
        if _required():
            pytest.fail("Set IMAGEJXX_SELECTION_CREATION_CPP_REPORT to the path-only installed case index")
        return None
    path = Path(configured).resolve(strict=True)
    index = _read(path)
    assert index["contract_id"] == CONTRACT and index["context"] == "imgui_cpp"
    assert index["case_count"] == 16 and type(index["case_count"]) is int
    assert index["acceptance_claim"] is False and index["observation_state"] == "observed"
    assert 0 <= _number(index["elapsed_seconds"]) <= 60
    assert [row["case_id"] for row in index["cases"]] == list(CASES)
    paths = {}
    for entry in index["cases"]:
        assert set(entry) <= {"id", "case_id", "relative_path", "byte_count", "sha256"}
        assert entry["id"] == entry["case_id"]
        member = _member(path.parent, entry["relative_path"])
        assert 0 < member.stat().st_size <= MAX_REPORT
        if "byte_count" in entry:
            assert member.stat().st_size == _integer(entry["byte_count"], 1)
        if "sha256" in entry:
            assert hashlib.sha256(member.read_bytes()).hexdigest() == entry["sha256"]
        paths[entry["case_id"]] = member
    assert len(set(paths.values())) == len(CASES)
    return paths


def _observe(case_id, context, directory, request):
    assert not directory.exists()
    started = time.monotonic()
    if context == "imgui_cpp":
        retained = request.getfixturevalue("standalone_cases")
        if retained is not None:
            path = retained[case_id]
            return _read(path), path.parent
        completed = subprocess.run([str(request.getfixturevalue("installed_cpp_executable")),
            "--selection-creation-ui-case", case_id, str(directory)], capture_output=True, timeout=60, check=False)
        assert len(completed.stdout) + len(completed.stderr) <= MAX_REPORT
        assert completed.returncode == 0, (completed.stdout + completed.stderr).decode("utf-8", "replace")
        row = _read(directory / "case.json")
    else:
        desktop = (importlib.import_module("imagejxx._desktop") if _required() else
                   pytest.importorskip("imagejxx._desktop", exc_type=ModuleNotFoundError))
        module = Path(desktop.__file__).resolve(strict=True)
        assert module.suffix in {".pyd", ".so"}
        if _required():
            roots = {Path(sysconfig.get_path(key)).resolve() / "imagejxx" for key in ("purelib", "platlib")}
            assert any(module.is_relative_to(root) for root in roots), "Checkout desktop import"
        editor = desktop.Editor(preferences_path=str(directory.parent / f"{case_id}-{context}-preferences.json"))
        row = _decode(editor._selection_creation_ui_case(case_id, context, str(directory)))
        assert row == _read(directory / "case.json")
    assert time.monotonic() - started < 60
    return row, directory


def _rect(box):
    x, y, w, h = box
    return frozenset((xx, yy) for yy in range(max(0, y), min(HEIGHT, y + h))
                     for xx in range(max(0, x), min(WIDTH, x + w)))


def _ellipse(box):
    x, y, w, h = box
    spans = {(4, 4): ((1, 3), (0, 4), (0, 4), (1, 3)),
             (8, 8): ((2, 6), (1, 7), (0, 8), (0, 8), (0, 8), (0, 8), (1, 7), (2, 6)),
             (7, 4): ((1, 6), (0, 7), (0, 7), (1, 6)),
             (7, 5): ((1, 6), (0, 7), (0, 7), (0, 7), (1, 6))}[(w, h)]
    return frozenset((x + xx, y + yy) for yy, (lo, hi) in enumerate(spans) for xx in range(lo, hi))


BASE_CELLS = _rect((4, 4, 10, 8))
HOLE_CELLS = _rect((8, 7, 4, 3))


def _rows(cells):
    return ["".join("1" if (x, y) in cells else "0" for x in range(WIDTH)) for y in range(HEIGHT)]


def _packed(cells):
    value = bytearray(WIDTH * HEIGHT // 8)
    for x, y in cells:
        assert 0 <= x < WIDTH and 0 <= y < HEIGHT
        index = y * WIDTH + x
        value[index // 8] |= 1 << (7 - index % 8)
    return bytes(value)


def _geometry(kind, points, width=1, attributes=None):
    return dict(copy.deepcopy(attributes or {}), kind=kind, points=[list(point) for point in points], width=width)


_EXPECTED_MASKS = {}


def _mask(cells):
    cells = frozenset(cells)
    encoded = base64.b64encode(_packed(cells)).decode("ascii")
    _EXPECTED_MASKS[encoded] = cells
    return {"kind": "mask", "points": [], "width": 1, "shape": [HEIGHT, WIDTH],
            "mask": encoded}


def _combine(base, candidate, operation):
    if operation == "add":
        return base | candidate
    if operation == "subtract":
        return base - candidate
    if operation == "intersect":
        return base & candidate
    if operation == "xor":
        return base ^ candidate
    assert operation == "replace"
    return candidate


# These literals are the owned P05l fixture, independently of producer setup.
CALIBRATION = {"spacing": [.5, 1.5, 2], "origin": [11, -7, 19],
               "direction": [1, 0, 0, 0, 1, 0, 0, 0, 1], "unit": "um",
               "frame_interval": .75, "time_origin": 2.25, "time_unit": "s"}
BASE = _geometry("rectangle", ((4, 4), (14, 12)), attributes={
    "label": "base", "imagejxx.roi_style": {"schema_version": 1,
        "stroke_color": [255, 213, 75, 255], "fill_color": [18, 52, 86, 64]}})
PREVIOUS = _geometry("line", ((1, 1), (3, 2)), attributes={"label": "previous"})
SMALL = _geometry("rectangle", ((1, 1), (5, 4)))
SMALL_CELLS = _rect((1, 1, 4, 3))
STATES = {
    "S01": "replace.opened replace.preview replace.published",
    "S02": "add.opened add.preview add.published",
    "S03": "subtract.opened subtract.preview subtract.published",
    "S04": "shift_alt.opened shift_alt.preview shift_alt.published",
    "S05": "early.opened early.preview early.key_released early.published late.opened late.key_pressed late.preview late.published",
    "S06": "intersect.overlap.opened intersect.overlap.preview intersect.overlap.published intersect.empty.opened intersect.empty.preview intersect.empty.published",
    "S07": "xor.hole.opened xor.hole.preview xor.hole.published xor.island.opened xor.island.preview xor.island.published",
    "S08": "ellipse.add.opened ellipse.add.preview ellipse.add.published ellipse.replace.opened ellipse.replace.preview ellipse.replace.published",
    "S09": "polygon.opened polygon.vertex2 polygon.vertex3 polygon.preview polygon.published",
    "S10": "freehand.opened freehand.preview freehand.published",
    "S11": "escape.opened escape.preview escape.cancelled tool.opened tool.preview tool.cancelled mode.opened mode.preview mode.cancelled",
    "S12": "owner.opened owner.preview owner.switched owner.returned owner.close.opened owner.close.preview owner.closed_draft owner.closed",
    "S13": "noop.prepared noop.opened noop.preview noop.published zero.opened zero.published",
    "S14": "busy.blocked focus.blocked plane.opened plane.preview plane.cancelled guards.complete",
    "S15": "record.original.opened record.original.preview record.original.published record.replayed rebind.destination rebind.opened rebind.preview rebind.published cancel_rebind.opened cancel_rebind.cancelled record.off_replayed saved reopened malformed.opened malformed.preview malformed.refused guards.complete",
    "S16": "absent_shift.opened absent_shift.preview absent_shift.published absent_alt.opened absent_alt.preview absent_alt.published nonarea.refused nonarea.replaced.opened nonarea.replaced.preview nonarea.replaced.published edit.preview edit.published scientific.guards",
}
APIS = {
    "S05": "base_reset_preparation", "S12": "",
    "S13": "exact_noop_base_preparation",
    "S14": "queued_apply_preparation busy_apply_refusal stale_selection_preparation stale_source_refusal foreign_owner_refusal invalid_candidate_refusal working_cap_refusal metadata_preparation stale_metadata_refusal close_owner_preparation closed_source_refusal",
    "S15": "replay_selection_preparation rebind_recording_off_selection_preparation workflow_malformed_preparation workflow_restore_preparation",
    "S16": "absent_shift_preparation absent_alt_preparation nonarea_base_preparation scientific_apply_refusal",
}


def _f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def _polygon_cells(points):
    """Pinned ImageJ PolygonFiller boundary rule, from primary source only.

    ShapeRoi.java1034-1057 hands float vertices to PolygonFiller. Its float
    edge table128-145 samples at y+.5 with a1e-8 tie bias; fill188-194 rounds
    intersections by truncating x+.5. This is independent of the native
    rasterizer. It matters when integer image requests become fractional
    coordinates after the actual screen point is floored.
    """
    assert len(points) >= 3
    base = [min(_f32(point[axis]) for point in points) for axis in range(2)]
    origin = [math.floor(value) for value in base]
    vertices = [[_f32(_f32(_f32(point[axis]) - base[axis]) + base[axis])
                 for axis in range(2)] for point in points]
    cells = set()
    for y in range(HEIGHT):
        intersections = []
        for first, second in zip(vertices, vertices[1:] + vertices[:1]):
            if first[1] > second[1]:
                first, second = second, first
            # All frozen points are nonnegative. ImageJ's Math.round edge-Y
            # admission and half-pixel sampling coincide on these paths.
            if math.floor(first[1] + .5) <= y < math.floor(second[1] + .5):
                slope = (second[0] - first[0]) / (second[1] - first[1])
                intersections.append(first[0] + (y + .5 - first[1]) * slope + 1e-8)
        intersections.sort()
        assert len(intersections) % 2 == 0
        for left, right in zip(intersections[::2], intersections[1::2]):
            cells.update((x, y) for x in range(max(0, int(left + .5)), min(WIDTH, int(right + .5))))
    assert all(origin[axis] >= 0 for axis in range(2))
    return frozenset(cells)


def _observed_selection(value, geometry, cells=None):
    if geometry is None:
        assert value is None
        return
    assert value["geometry"] == geometry and value["mask_shape"] == [HEIGHT, WIDTH]
    assert isinstance(geometry["kind"], str) and _number(geometry["width"]) > 0
    assert all(isinstance(point, list) and len(point) == 2 and all(math.isfinite(_number(v)) for v in point)
               for point in geometry["points"])
    assert value["mask_error"] is None, value["mask_error"]
    rows = value["mask_rows"]
    assert len(rows) == HEIGHT and all(len(line) == WIDTH and set(line) <= {"0", "1"} for line in rows)
    if cells is not None:
        assert rows == _rows(cells)
    if geometry["kind"] == "mask":
        assert cells is not None
        assert geometry == _mask(cells) and value["packed_mask_hex"] == _packed(cells).hex()
    else:
        assert value["packed_mask_hex"] is None


def _selection(document, geometry, cells=None, *, previous=False):
    key = "previous_selection" if previous else "selection"
    assert document[key] == geometry
    _observed_selection(document[key + "_observation"], geometry, cells)


def _owner(state, document_id):
    matches = [document for document in state["documents"] if document["document_id"] == document_id]
    assert len(matches) == 1
    return matches[0]


def _state(row, label, document_id=None):
    state = row["states"][label]
    frame = row["frames"][_integer(state["frame_ordinal"], 1) - 1]
    assert frame["label"] == label
    assert state["document_ids"] == state["facade_ids"] == [doc["document_id"] for doc in state["documents"]]
    assert len(set(state["document_ids"])) == len(state["document_ids"]) <= 16
    assert state["active_document_id"] in state["document_ids"] or state["active_document_id"] == 0
    assert type(state["recording"]) is bool
    return state, frame["rendered"], _owner(state, document_id or row["source_document_id"])


def _document_unchanged(before, after, *, display=True, selection=True):
    excluded = {"revisions"}
    if not display:
        excluded.add("display")
    if not selection:
        excluded.update({"selection", "previous_selection", "selection_observation", "previous_selection_observation"})
    assert set(after) == set(before)
    for name in set(before) - excluded:
        assert _typed(after[name]) == _typed(before[name]), name
    for name in ("pixels", "metadata") + (("selection",) if selection else ()) + (("display",) if display else ()):
        assert after["revisions"][name] == before["revisions"][name], name


def _collection_unchanged(before, after, *, display=True):
    for name in ("document_ids", "facade_ids", "active_document_id", "recording", "workflow", "results"):
        assert _typed(before[name]) == _typed(after[name]), name
    for document in before["documents"]:
        _document_unchanged(document, _owner(after, document["document_id"]), display=display)


def _literal(document, scientific=False):
    assert document["shape"] == ([2, 2, 2, 24, 32, 1] if scientific else [1, 1, 2, 24, 32, 1])
    assert document["dtype"] == "uint16" and document["sample_policy"] == "finite_only"
    assert document["calibration"] == CALIBRATION
    positions = [(t, c, z) for t in range(document["shape"][0])
                 for c in range(document["shape"][1]) for z in range(2)]
    assert len(document["planes"]) == len(positions)
    for plane, position in zip(document["planes"], positions):
        assert plane["position"] == list(position)
        samples = [7 * position[2] + 3 * y + x for y in range(HEIGHT) for x in range(WIDTH)]
        assert plane["declared_hex"] == struct.pack("<" + "H" * len(samples), *samples).hex()
        assert plane["storage_f64_hex"] == struct.pack("<" + "d" * len(samples), *samples).hex()
        for name in ("image_address", "buffer_address"):
            assert isinstance(plane[name], str) and plane[name].startswith("0x") and int(plane[name], 16) > 0


def _fixtures(row):
    labels = {"source"} | ({"other"} if row["case_id"] in {"S12", "S14", "S15"} else
                           {"scientific"} if row["case_id"] == "S16" else set())
    assert set(row["fixture_sources"]) == labels
    initial, _, source = _state(row, "initial")
    assert initial["recording"] is True and initial["workflow"] == initial["results"] == []
    assert initial["active_document_id"] == row["source_document_id"]
    assert {doc["document_id"] for doc in row["fixture_sources"].values()} == set(initial["document_ids"])
    for name, document in row["fixture_sources"].items():
        _literal(document, name == "scientific")
        _document_unchanged(document, _owner(initial, document["document_id"]))
        _selection(document, BASE, BASE_CELLS)
        _selection(document, PREVIOUS, previous=True)
        assert document["dirty"] is False and document["undo_steps"] == document["redo_steps"] == 1
        assert document["history"]["undo_steps"] == document["history"]["redo_steps"] == 1
        assert _typed(document["metadata"]["properties"]) == _typed({"source_only": {"nested": [True, 7, "retained"]}, "revision_probe": 7})
        assert _typed(document["metadata"]["extensions"]) == _typed({"future_annotation": {"version": 37, "opaque": [3, 1, 4]}})
        assert _typed(document["metadata"]["record_layout"]) == _typed({"source_presence": True})
        assert document["rois"] == [{"name": "stored", "selection": _geometry("rectangle", ((2, 2), (6, 6))),
                                    "t": None, "c": None, "z": None, "attributes": {"note": "retained"}}]
        assert document["overlays"] == [{"kind": "roi", "slice_index": 1,
            "selection": _geometry("rectangle", ((1, 1), (3, 3)), attributes={"imagejxx.roi_style": {
                "schema_version": 1, "fill_color": [20, 80, 140, 96], "stroke_color": None}})}]
        assert document["spectrum_present"] is False
        display = document["display"]
        assert display["t"] == display["c"] == 0 and display["crosshair"] == [16, 12, 1]
        assert display["orthogonal"] is False and display["linked_zoom"] is False and display["zoom"] == 0
    return initial, source


def _control(rendered, selector):
    if selector.startswith("document."):
        values = [tab for tab in rendered["document_tabs"] if tab["document_id"] == int(selector[9:])]
        assert len(values) == 1
        return values[0]
    if selector.startswith("error_modal."):
        return rendered["error_modal"]["controls"][selector[12:]]
    return rendered["host_controls"][selector]


def _canvas(rendered):
    ordinary, navigation = rendered["ordinary_canvas"], rendered["orthogonal_navigation"]
    point, extent = navigation["ordinary_canvas_point"], ordinary["extent"]
    return {"id": navigation["ordinary_canvas_id"], "point": point,
            "rect": [point[0] - extent[0] / 2, point[1] - extent[1] / 2,
                     point[0] + extent[0] / 2, point[1] + extent[1] / 2],
            "enabled": True, "focused": navigation["ordinary_canvas_focused"],
            "geometry_source": "actual_ordinary_canvas", "origin": ordinary["origin"],
            "scale": ordinary["scale"], "extent": extent}


def _physical(row):
    frames = row["frames"]
    assert 1 <= row["frame_count"] == len(frames) <= 180
    assert [frame["ordinal"] for frame in frames] == list(range(1, len(frames) + 1))
    assert len(row["draft_observations"]) == len(frames)
    held, buttons, position, sessions = set(), [False] * 3, None, []
    for frame, observed in zip(frames, row["draft_observations"]):
        rendered, requested = frame["rendered"], frame["requested_events"]
        actual, session = rendered["input_frame"], rendered["session"]
        assert session["open"] is True and _integer(session["context_id"], 1) == session["current_context_id"]
        sessions.append(session)
        assert actual["raw_inputs"] == requested
        assert observed == frame["draft_observation"] and observed["frame_ordinal"] == frame["ordinal"]
        assert observed["selection_creation"] == rendered["selection_creation"]
        for event in requested:
            kind = event["type"]
            if kind == "position":
                assert set(event) == {"type", "space", "point"} and event["space"] == "screen"
                position = event["point"]
                assert len(position) == 2 and all(_number(v) == math.floor(v) for v in position)
            elif kind == "button":
                assert set(event) == {"type", "name", "down"} and event["name"] == "left" and type(event["down"]) is bool
                assert buttons[0] is not event["down"], "Duplicate button transition"
                buttons[0] = event["down"]
            elif kind in {"keys", "key"}:
                atoms = event["keys"] if kind == "keys" else [{"name": event["name"], "down": event["down"]}]
                assert atoms and len({atom["name"] for atom in atoms}) == len(atoms)
                for atom in atoms:
                    assert set(atom) == {"name", "down"} and atom["name"] in KEYS and type(atom["down"]) is bool
                    (held.add if atom["down"] else held.discard)(atom["name"])
            elif kind == "text":
                assert set(event) == {"type", "value"} and isinstance(event["value"], str) and "\x00" not in event["value"]
                assert actual["input_text"]["id"] == actual["focus"]["active_id"] != 0
            elif kind == "wheel":
                wheels = {("S14", "plane.wheel"): -1,
                          ("S15", "record.off_scroll.down"): -3,
                          ("S15", "record.off_scroll.up"): 3}
                assert (row["case_id"], frame["label"]) in wheels
                assert event == {"type": "wheel", "x": 0, "y": wheels[row["case_id"], frame["label"]]}
            else:
                raise AssertionError(f"Undeclared physical input {kind!r}")
        if position is not None:
            assert actual["mouse_position"] == position
        assert actual["mouse_down"] == buttons and set(actual["keys_down"]) == held
        assert actual["modifiers"] == {"ctrl": "Ctrl" in held, "shift": "Shift" in held,
                                       "alt": "Alt" in held, "super": "Super" in held}
        _resources(rendered["selection_creation"])
    assert not held and buttons == [False] * 3
    assert len({session["context_id"] for session in sessions}) == len({session["session_id"] for session in sessions}) == 1
    for before, after in zip(sessions, sessions[1:]):
        assert after["imgui_frame"] == before["imgui_frame"] + 1 and after["imgui_time"] > before["imgui_time"]
    accounted = set()
    for observed in row["control_observations"]:
        ordinal = _integer(observed["before_frame_ordinal"], 1)
        assert ordinal + 3 <= len(frames)
        control = _control(frames[ordinal - 1]["rendered"], observed["selector"])
        assert observed["control"] == control and _integer(control["id"], 1)
        assert type(observed["disabled_attempt"]) is bool and control["enabled"] is (not observed["disabled_attempt"])
        point, rect = observed["position"], control["rect"]
        assert point == [math.floor(_number(value)) for value in control["point"]]
        assert len(rect) == 4 and rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]
        assert all(0 <= value < limit for value, limit in zip(point, HEADLESS_VIEWPORT)), "Control point is outside the actual headless viewport"
        assert [f["requested_events"] for f in frames[ordinal:ordinal + 3]] == [
            [{"type": "position", "space": "screen", "point": point}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}]]
        owned = set(range(ordinal + 1, ordinal + 4))
        assert not accounted & owned
        accounted |= owned
    gesture_frames = set()
    for observed in row["gesture_observations"]:
        assert set(observed) == {"label", "action", "requested_image_point", "delivered_screen_point", "control",
                                 "before_frame_ordinal", "after_frame_ordinal"}
        before = _integer(observed["before_frame_ordinal"], 1)
        after = _integer(observed["after_frame_ordinal"], 1)
        assert after == before + 1 <= len(frames) and after not in gesture_frames and after not in accounted
        gesture_frames.add(after)
        previous, frame = frames[before - 1], frames[after - 1]
        assert frame["label"] == observed["label"]
        control = _canvas(previous["rendered"])
        # The busy bit belongs to the actual native collection, avoiding a
        # synthetic enabled value if the rendered root has no duplicate bit.
        control["enabled"] = not previous["native_state"]["busy"]
        assert observed["control"] == control and _integer(control["id"], 1)
        assert all(_number(v) > 0 for v in control["scale"] + control["extent"])
        assert len(control["origin"]) == 2
        wanted, screen = observed["requested_image_point"], observed["delivered_screen_point"]
        assert len(wanted) == 2 and all(type(value) is int for value in wanted)
        assert 0 <= wanted[0] <= WIDTH and 0 <= wanted[1] <= HEIGHT
        assert screen == [math.floor(control["origin"][i] + wanted[i] * control["scale"][i]) for i in range(2)]
        assert all(_number(value) == math.floor(value) for value in screen)
        rect = control["rect"]
        assert rect[0] <= screen[0] < rect[2] and rect[1] <= screen[1] < rect[3]
        action = observed["action"]
        if action in {"position", "drag"}:
            assert frame["requested_events"] == [{"type": "position", "space": "screen", "point": screen}]
            assert frame["rendered"]["input_frame"]["mouse_down"][0] is (action == "drag")
        else:
            assert action in {"press", "release"}
            assert frame["requested_events"] == [{"type": "button", "name": "left", "down": action == "press"}]
            assert frame["rendered"]["input_frame"]["mouse_position"] == screen
    for frame in frames:
        for event in frame["requested_events"]:
            if event["type"] in {"button", "position"}:
                assert frame["ordinal"] in accounted | gesture_frames, "Unattributed physical pointer input"
    _workflow_scrolls(row)
    opened, closed = row["lifecycle"]
    assert opened["open"] is True and closed["open"] is False
    assert opened["session_id"] == closed["session_id"] == sessions[0]["session_id"]
    assert closed["context_id"] == closed["current_context_id"] == 0


def _workflow_scrolls(row):
    scrolls = row["scroll_observations"]
    wheel_labels = [frame["label"] for frame in row["frames"]
                    if any(event["type"] == "wheel" for event in frame["requested_events"])]
    expected_wheels = ["plane.wheel"] if row["case_id"] == "S14" else [
        "record.off_scroll.down", "record.off_scroll.up"] if row["case_id"] == "S15" else []
    assert wheel_labels == expected_wheels
    assert isinstance(scrolls, list) and len(scrolls) == (2 if row["case_id"] == "S15" else 0)
    if not scrolls:
        return
    anchors = ("workflow.record", "workflow.selection_creation.0.replay", "workflow.selection_creation.2.replay")
    initial_controls, restored_controls = None, None
    for observed, direction, selector in zip(scrolls, (-3, 3), (anchors[0], anchors[2])):
        label = "record.off_scroll." + ("down" if direction < 0 else "up")
        assert set(observed) == {"label", "selector", "control", "mouse_position", "viewport_size",
            "before_frame_ordinal", "wheel_frame_ordinal", "settled_frame_ordinal", "control_after"}
        assert observed["label"] == label and observed["selector"] == selector
        ordinal = _integer(observed["before_frame_ordinal"], 1)
        assert observed["wheel_frame_ordinal"] == ordinal + 1
        assert observed["settled_frame_ordinal"] == ordinal + 2 <= len(row["frames"])
        before, wheel, settled = row["frames"][ordinal - 1:ordinal + 2]
        assert wheel["label"] == label and settled["label"] == label + ".settled"
        assert wheel["requested_events"] == [{"type": "wheel", "x": 0, "y": direction}]
        assert settled["requested_events"] == []
        assert observed["viewport_size"] == list(HEADLESS_VIEWPORT)
        assert all(_number(value) > 0 for value in observed["viewport_size"])
        control = _control(before["rendered"], selector)
        assert observed["control"] == control and _integer(control["id"], 1)
        assert control["enabled"] is True
        mouse = observed["mouse_position"]
        assert mouse == [math.floor(_number(value)) for value in control["point"]]
        assert all(0 <= value < size for value, size in zip(mouse, HEADLESS_VIEWPORT))
        assert control["rect"][0] <= mouse[0] < control["rect"][2] and control["rect"][1] <= mouse[1] < control["rect"][3]
        assert before["rendered"]["input_frame"]["mouse_position"] == mouse
        prior_clicks = [item for item in row["control_observations"] if item["before_frame_ordinal"] + 3 <= ordinal]
        prior = max(prior_clicks, key=lambda item: item["before_frame_ordinal"])
        assert prior["selector"] == selector and prior["disabled_attempt"] is False and prior["position"] == mouse
        assert all(not frame["requested_events"] for frame in row["frames"][prior["before_frame_ordinal"] + 3:ordinal])
        if direction < 0:
            preparation = _api(row, "rebind_recording_off_selection_preparation")
            assert preparation["before_frame_ordinal"] == ordinal
            baseline = preparation["after"]
        else:
            assert before["label"] == "record.off_replayed"
            baseline = before["native_state"]
        assert baseline["recording"] is False and baseline["busy"] is False
        for frame in (wheel, settled):
            actual, state = frame["rendered"]["input_frame"], frame["native_state"]
            assert actual["mouse_position"] == mouse and actual["mouse_down"] == [False] * 3
            assert actual["keys_down"] == [] and actual["modifiers"] == {key: False for key in ("ctrl", "shift", "alt", "super")}
            assert actual["popup_stack"] == [] and actual["focus"]["window"] == "Workflow"
            for key in ("document_ids", "facade_ids", "active_document_id", "workflow", "results", "recording", "busy", "usage"):
                assert _typed(state[key]) == _typed(baseline[key]), key
            for document in state["documents"]:
                original = _owner(baseline, document["document_id"])
                assert _typed(document) == _typed({key: original[key] for key in document})
            _resources(frame["rendered"]["selection_creation"], False)
            for key in ("origin", "scale", "extent"):
                assert frame["rendered"]["ordinary_canvas"][key] == before["rendered"]["ordinary_canvas"][key]
        assert observed["control_after"] == _control(settled["rendered"], selector)
        original_controls = {name: _control(before["rendered"], name) for name in anchors}
        shifted_controls = {name: _control(settled["rendered"], name) for name in anchors}
        delta = shifted_controls[anchors[0]]["rect"][1] - original_controls[anchors[0]]["rect"][1]
        assert delta < 0 if direction < 0 else delta > 0
        for name in anchors:
            first, last = original_controls[name], shifted_controls[name]
            assert last["id"] == first["id"] and last["enabled"] is first["enabled"] is True
            assert last["rect"] == [first["rect"][0], _f32(first["rect"][1] + delta),
                                    first["rect"][2], _f32(first["rect"][3] + delta)]
            assert last["point"] == [first["point"][0], _f32(first["point"][1] + delta)]
        if direction < 0:
            initial_controls = original_controls
            before_point, visible_point = original_controls[anchors[2]]["point"], shifted_controls[anchors[2]]["point"]
            assert before_point[1] >= HEADLESS_VIEWPORT[1]
            assert all(0 <= math.floor(value) < bound for value, bound in zip(visible_point, HEADLESS_VIEWPORT))
            replay_clicks = [item for item in row["control_observations"] if item["selector"] == anchors[2]]
            assert len(replay_clicks) == 1 and replay_clicks[0]["before_frame_ordinal"] == settled["ordinal"]
            assert replay_clicks[0]["control"] == shifted_controls[anchors[2]]
        else:
            restored_controls = shifted_controls
            record_clicks = [item for item in row["control_observations"] if item["selector"] == anchors[0]
                             and item["before_frame_ordinal"] >= settled["ordinal"]]
            assert len(record_clicks) == 1 and record_clicks[0]["before_frame_ordinal"] == settled["ordinal"]
            assert record_clicks[0]["control"] == shifted_controls[anchors[0]]
    for name in anchors:
        for key in ("id", "point", "rect", "enabled"):
            assert restored_controls[name][key] == initial_controls[name][key]


def _resources(diagnostic, active=None):
    count = _integer(diagnostic["retained_draft_count"])
    retained = _integer(diagnostic["total_retained_bytes"])
    owners = diagnostic["retained_owner_ids"]
    assert len(owners) == len(set(owners)) == count <= 1
    draft = diagnostic["draft"]
    if draft is None:
        assert count == retained == 0 and owners == []
    else:
        assert count == 1 and owners == [draft["document_id"]]
        assert retained == _integer(draft["retained_bytes"])
        assert (draft["candidate"] is None) is (draft["preview"] is None)
        assert retained > 0 if draft["candidate"] is not None else retained == 0
    if active is not None:
        assert (draft is not None) is active


def _api(row, name, *, refused=False):
    matches = [item for item in row["api_observations"] if item["name"] == name]
    assert len(matches) == 1
    item = matches[0]
    assert "API" in item["role"] and "not physical" in item["role"]
    assert item["accepted"] is (not refused)
    if refused:
        assert item["error_type"] and item["error"]
        expected_type = {"stale_source_refusal": "revision_conflict", "stale_metadata_refusal": "revision_conflict",
                         "foreign_owner_refusal": "out_of_range", "closed_source_refusal": "out_of_range"}.get(name, "invalid_argument")
        assert item["error_type"] == expected_type
        _collection_unchanged(item["before"], item["after"])
    return item


def _selectors(row, *selectors, disabled=False):
    found = {item["selector"] for item in row["control_observations"] if item["disabled_attempt"] is disabled}
    assert set(selectors) <= found


def _frame(row, label):
    matches = [frame for frame in row["frames"] if frame["label"] == label]
    assert len(matches) == 1, label
    return matches[0]


def _pointer(row, label, point=None, action=None, modifiers=None):
    matches = [item for item in row["gesture_observations"] if item["label"] == label]
    assert len(matches) == 1, label
    item = matches[0]
    if point is not None:
        assert item["requested_image_point"] == list(point)
        row.setdefault("_checked_gesture_labels", set()).add(label)
    if action is not None:
        assert item["action"] == action
    frame = row["frames"][item["after_frame_ordinal"] - 1]
    if modifiers is not None:
        actual = frame["rendered"]["input_frame"]["modifiers"]
        assert actual == {"ctrl": "Ctrl" in modifiers, "shift": "Shift" in modifiers,
                          "alt": "Alt" in modifiers, "super": "Super" in modifiers}
    control = item["control"]
    # Existing Window canvas mapping (pre-P05l source snapshot010:5940-5941)
    # evaluates float ImVec2 subtraction and float division before assigning
    # to double. Reported scale already includes the binary32 zoom*aspect.
    # Preserve each IEEE754 rounding; geometry comparisons remain exact.
    values = [_f32(_f32(_f32(item["delivered_screen_point"][axis]) - _f32(control["origin"][axis])) /
                   _f32(control["scale"][axis]))
              for axis in range(2)]
    return item, values


def _record(value, source, candidate, result, operation, base=None):
    expected = {"schema": "imagejxx.selection-creation-command", "version": 1,
        "source": {"id": source["document_id"], "pixels_revision": source["revisions"]["pixels"],
                   "metadata_revision": source["revisions"]["metadata"], "selection_revision": source["revisions"]["selection"],
                   "shape": source["shape"], "type": source["dtype"], "sample_policy": source["sample_policy"]},
        "position": {"t": source["display"]["t"], "c": source["display"]["c"], "z": source["display"]["crosshair"][2]},
        "operation": operation, "base": source["selection"] if base is None else base,
        "candidate": candidate, "result": result}
    assert value == expected
    assert type(value["version"]) is int
    for key in ("id", "pixels_revision", "metadata_revision", "selection_revision"):
        _integer(value["source"][key], 1 if key == "id" else 0)
    assert all(type(v) is int for v in value["source"]["shape"] + list(value["position"].values()))


def _published(before, after, document_id, geometry, cells, *, candidate=None, operation=None,
               record=None, recording=None):
    first, last = _owner(before, document_id), _owner(after, document_id)
    changed = first["selection"] != geometry
    _document_unchanged(first, last, selection=not changed)
    _selection(last, geometry, cells)
    if changed:
        _selection(last, first["selection"], previous=True,
                   cells=_known_cells(first["selection"]) if first["selection"] and first["selection"]["kind"] == "mask" else None)
        assert last["revisions"]["selection"] == first["revisions"]["selection"] + 1
        assert last["previous_selection_observation"] == first["selection_observation"]
    for key in ("document_ids", "facade_ids", "active_document_id", "recording", "results"):
        assert _typed(after[key]) == _typed(before[key]), key
    for owner in before["documents"]:
        if owner["document_id"] != document_id:
            _document_unchanged(owner, _owner(after, owner["document_id"]))
    recording = before["recording"] if recording is None else recording
    if changed and recording:
        assert isinstance(before["workflow"], list) and len(after["workflow"]) == len(before["workflow"]) + 1
        assert after["workflow"][:-1] == before["workflow"]
        if record is not None:
            assert after["workflow"][-1] == record
        else:
            _record(after["workflow"][-1], first, candidate, geometry, operation)
    else:
        assert after["workflow"] == before["workflow"]
    return last


def _known_cells(geometry):
    """Only source-qualified geometry or masks authored by this reader."""
    kind = geometry["kind"]
    if kind == "mask":
        assert geometry == _mask(_EXPECTED_MASKS[geometry["mask"]])
        return _EXPECTED_MASKS[geometry["mask"]]
    if kind in {"rectangle", "ellipse"}:
        first, last = geometry["points"]
        box = tuple(math.floor(value + .5) for value in
                    (min(first[0], last[0]), min(first[1], last[1]), abs(last[0] - first[0]), abs(last[1] - first[1])))
        return _box_cells(kind, box)
    assert kind in {"polygon", "freehand"}
    return _polygon_cells(geometry["points"])


def _draft(row, label, before, document_id, tool, operation, candidate=None, cells=None, *, start=None):
    state, rendered, document = _state(row, label, document_id)
    _collection_unchanged(before, state)
    diagnostic = rendered["selection_creation"]
    _resources(diagnostic, True)
    draft = diagnostic["draft"]
    source = _owner(before, document_id)
    assert draft["document_id"] == document_id and draft["tool"] == tool and draft["operation"] == operation
    assert draft["source_selection"] == source["selection"] and draft["source_revisions"] == source["revisions"]
    assert draft["position"] == {"t": source["display"]["t"], "c": source["display"]["c"], "z": source["display"]["crosshair"][2]}
    assert draft["candidate"] == candidate
    assert rendered["orthogonal_navigation"]["ordinary_canvas_focused"] is True
    assert diagnostic["admission"] == "admitted"
    observed = row["frames"][state["frame_ordinal"] - 1]["draft_observation"]
    _observed_selection(observed["candidate_observation"], candidate, cells)
    if candidate is None:
        assert draft["preview"] is None
        _observed_selection(observed["preview_observation"], None)
    else:
        if operation == "replace" or source["selection"] is None:
            expected, result_cells = candidate, cells
        else:
            # Original base masks are validated against independent case
            # literals before this helper is called.
            result_cells = _combine(_known_cells(source["selection"]), cells, operation)
            expected = _mask(result_cells)
        assert draft["preview"] == expected
        _observed_selection(observed["preview_observation"], expected, result_cells)
    if start is not None:
        item, raw = _pointer(row, start, action="press")
        expected_start = [max(0, min(bound, math.floor(value + .5))) for bound, value in zip((WIDTH, HEIGHT), raw)] if tool in {"rectangle", "ellipse"} else raw
        assert draft["starting_pointer"] == expected_start
        assert draft["canvas_id"] == item["control"]["id"]
        assert draft["canvas_origin"] == item["control"]["origin"] and draft["canvas_scale"] == item["control"]["scale"]
        assert draft["canvas_extent"] == item["control"]["extent"]
        press_frame = row["frames"][item["after_frame_ordinal"] - 1]
        assert press_frame["rendered"]["input_frame"]["focus"]["active_id"] == draft["canvas_id"]
    return state, rendered, document


def _released(row, label):
    state, rendered, _ = _state(row, label)
    _resources(rendered["selection_creation"], False)
    return state


def _box_geometry(kind, box):
    x, y, width, height = box
    return _geometry(kind, ((x, y), (x + width, y + height)))


def _box_cells(kind, box):
    return _ellipse(box) if kind == "ellipse" else _rect(box)


def _draft_frame(row, frame, before, document_id, tool, operation, candidate, cells):
    """Check the actual transient at an input frame, not just checkpoints."""
    summary, diagnostic = frame["native_state"], frame["rendered"]["selection_creation"]
    for key in ("document_ids", "facade_ids", "active_document_id", "workflow", "results", "recording"):
        assert summary[key] == before[key], key
    for original in before["documents"]:
        current = _owner(summary, original["document_id"])
        for key in ("document_id", "shape", "dtype", "sample_policy", "selection", "previous_selection",
                    "selection_observation", "previous_selection_observation", "revisions", "display", "data_identity",
                    "dirty", "undo_steps", "redo_steps"):
            assert _typed(current[key]) == _typed(original[key]), key
    _resources(diagnostic, True)
    draft = diagnostic["draft"]
    original = _owner(before, document_id)
    assert draft["document_id"] == document_id and draft["tool"] == tool and draft["operation"] == operation
    assert draft["source_selection"] == original["selection"] and draft["source_revisions"] == original["revisions"]
    assert draft["candidate"] == candidate
    assert frame["rendered"]["input_frame"]["focus"]["active_id"] == draft["canvas_id"]
    assert frame["rendered"]["orthogonal_navigation"]["ordinary_canvas_focused"] is True
    assert diagnostic["admission"] == "admitted"
    observed = frame["draft_observation"]
    _observed_selection(observed["candidate_observation"], candidate, cells)
    if candidate is None:
        assert draft["preview"] is None
        _observed_selection(observed["preview_observation"], None)
    else:
        if operation == "replace" or original["selection"] is None:
            result, result_cells = candidate, cells
        else:
            result_cells = _combine(_known_cells(original["selection"]), cells, operation)
            result = _mask(result_cells)
        assert draft["preview"] == result
        _observed_selection(observed["preview_observation"], result, result_cells)


def _basic(row, prefix, before, operation, start, points, boxes, *, modifiers=(), tool="rectangle",
           document_id=None, drag_labels=None, published=None):
    document_id = document_id or row["source_document_id"]
    _selectors(row, "tools.tool." + tool)
    _pointer(row, prefix + ".press.position", start, "position", modifiers)
    _pointer(row, prefix + ".press", start, "press", modifiers)
    _draft(row, prefix + ".opened", before, document_id, tool, operation, start=prefix + ".press")
    assert len(points) == len(boxes)
    for index, (point, box) in enumerate(zip(points, boxes), 1):
        label = drag_labels[index - 1] if drag_labels else prefix + f".drag.{index}"
        _pointer(row, label, point, "drag", modifiers)
        candidate, cells = _box_geometry(tool, box), _box_cells(tool, box)
        _draft_frame(row, _frame(row, label), before, document_id, tool, operation, candidate, cells)
    candidate, cells = _box_geometry(tool, boxes[-1]), _box_cells(tool, boxes[-1])
    _draft(row, prefix + ".preview", before, document_id, tool, operation, candidate, cells, start=prefix + ".press")
    _pointer(row, prefix + ".release", points[-1], "release", modifiers)
    label = published or prefix + ".published"
    after, rendered, _ = _state(row, label, document_id)
    _resources(rendered["selection_creation"], False)
    source = _owner(before, document_id)
    result_cells = cells if operation == "replace" or source["selection"] is None else _combine(
        _known_cells(source["selection"]), cells, operation)
    result = candidate if operation == "replace" or source["selection"] is None else _mask(result_cells)
    _published(before, after, document_id, result, result_cells, candidate=candidate, operation=operation)
    return after


def _cancelled(row, label, before, *, reason, document_id=None):
    state, rendered, _ = _state(row, label, document_id)
    _collection_unchanged(before, state)
    _resources(rendered["selection_creation"], False)
    assert rendered["selection_creation"]["last_cancel"] == reason
    return state


def _cancellation_boundary(row, preview_label, final_label, selectors, *, semantic=None):
    """Derive the first cause from raw focus/popup/widget/owner facts.

    Moving to a tool, mode or File menu can remove admitted canvas focus on
    mouse-down before the later semantic action. A freed draft cannot acquire
    a later cancellation reason. No list of alternative reason strings is
    accepted: the earliest observed input boundary determines one cause.
    """
    preview = row["states"][preview_label]
    final = row["states"][final_label]
    first, last = preview["frame_ordinal"], final["frame_ordinal"]
    initial_frame = row["frames"][first - 1]
    retained = initial_frame["rendered"]["selection_creation"]["draft"]
    assert retained is not None
    owner_id, canvas_id = retained["document_id"], retained["canvas_id"]
    canvas_window = initial_frame["rendered"]["input_frame"]["focus"]["window"]
    assert canvas_window and initial_frame["rendered"]["input_frame"]["popup_stack"] == []
    initial_mode = initial_frame["rendered"]["selection_creation"]["mode"]
    clicks = [item for item in row["control_observations"] if item["selector"] in selectors
              and first <= item["before_frame_ordinal"] < last]
    assert {item["selector"] for item in clicks} == set(selectors)
    boundaries = {ordinal for item in clicks for ordinal in range(item["before_frame_ordinal"] + 1,
                                                                 item["before_frame_ordinal"] + 4)}
    tool_releases = {item["before_frame_ordinal"] + 3 for item in clicks if item["selector"].startswith("tools.tool.")}
    document_downs = {item["before_frame_ordinal"] + 2: item for item in clicks if item["selector"].startswith("document.")}
    cause, pending_tab = None, None
    for frame in row["frames"][first:last]:
        diagnostic, actual, state = frame["rendered"]["selection_creation"], frame["rendered"]["input_frame"], frame["native_state"]
        for key in ("workflow", "results", "recording"):
            assert state[key] == preview[key], key
        for original in preview["documents"]:
            if original["document_id"] not in state["document_ids"]:
                continue
            observed_owner = _owner(state, original["document_id"])
            for key in ("shape", "dtype", "sample_policy", "selection", "previous_selection", "selection_observation",
                        "previous_selection_observation", "revisions", "display", "data_identity", "dirty", "undo_steps", "redo_steps"):
                assert _typed(observed_owner[key]) == _typed(original[key]), key
        expected = None
        if cause is None:
            if pending_tab is not None:
                target_id, target_selector, up_ordinal = pending_tab
                assert frame["ordinal"] == up_ordinal and frame["label"] == target_selector + ".up"
                assert actual["raw_inputs"] == [{"type": "button", "name": "left", "down": False}]
                assert actual["mouse_down"] == [False] * 3
                assert state["active_document_id"] == target_id and state["document_ids"] == preview["document_ids"]
                tabs = {tab["document_id"]: tab for tab in frame["rendered"]["document_tabs"]}
                assert tabs[target_id]["selected"] is True and tabs[owner_id]["selected"] is False
                cause, pending_tab = "owner_changed", None
                _resources(diagnostic, False)
                assert diagnostic["last_cancel"] == cause
                continue
            tab_click = document_downs.get(frame["ordinal"])
            if tab_click is not None:
                target_id = int(tab_click["selector"][9:])
                tabs = {tab["document_id"]: tab for tab in frame["rendered"]["document_tabs"]}
                source_tab, target_tab = tabs[owner_id], tabs[target_id]
                if (state["active_document_id"] == owner_id and source_tab["selected"] is True and
                        target_tab["selected"] is False and target_tab["active"] is True and target_tab["focused"] is True):
                    # The earlier source canvas ran before the later tab item
                    # handled this down event. BeginTabItem applies ownership
                    # on the following frame; this is not a generic widget
                    # focus exemption. Retain exactly this draft until handoff.
                    assert target_id != owner_id and frame["label"] == tab_click["selector"] + ".down"
                    assert actual["raw_inputs"] == [{"type": "button", "name": "left", "down": True}]
                    assert actual["mouse_down"] == [True, False, False]
                    assert actual["focus"]["active_id"] == target_tab["id"] == tab_click["control"]["id"]
                    assert actual["popup_stack"] == [] and actual["focus"]["window"] == canvas_window
                    assert state["document_ids"] == preview["document_ids"]
                    assert _typed(diagnostic["draft"]) == _typed(retained)
                    assert diagnostic["last_cancel"] == initial_frame["rendered"]["selection_creation"]["last_cancel"]
                    _resources(diagnostic, True)
                    pending_tab = target_id, tab_click["selector"], frame["ordinal"] + 1
                    continue
            if owner_id not in state["document_ids"]:
                expected = "owner_closed"
            elif state["active_document_id"] != owner_id:
                expected = "owner_changed"
            elif diagnostic["mode"] != initial_mode:
                expected = "mode_changed"
            elif semantic == "tool_changed" and frame["ordinal"] in tool_releases:
                expected = "tool_changed"
            elif actual["popup_stack"]:
                expected = "popup"
            elif actual["focus"]["window"] != canvas_window:
                expected = "focus_lost"
            elif actual["focus"]["active_id"] not in (0, canvas_id):
                expected = "other_widget"
            if expected is None:
                assert diagnostic["draft"] == retained
                _resources(diagnostic, True)
                continue
            assert frame["ordinal"] in boundaries, "Draft cleared outside the declared physical control boundary"
            assert any(event["type"] in {"position", "button"} for event in frame["requested_events"])
            if expected in {"owner_closed", "owner_changed", "mode_changed", "tool_changed"}:
                assert any(event["type"] == "button" for event in frame["requested_events"])
            cause = expected
        _resources(diagnostic, False)
        assert diagnostic["last_cancel"] == cause
    assert cause is not None and pending_tab is None, "The declared physical boundary did not cancel the draft"
    return cause


def _polygon(row, prefix, before, count=3, operation="replace", *, document_id=None):
    document_id = document_id or row["source_document_id"]
    points = ((18, 4), (25, 4), (25, 11), (18, 11))
    actual = []
    for index, wanted in enumerate(points[:count], 1):
        label = prefix + f".vertex.{index}.press"
        if index > 1:
            _pointer(row, prefix + f".vertex.{index}.move", wanted, "position")
        item, point = _pointer(row, label, wanted, "press", ("Shift",) if operation == "add" and index == 1 else ())
        _pointer(row, label + ".position", wanted, "position")
        _pointer(row, prefix + f".vertex.{index}.release", wanted, "release")
        actual.append(point)
        candidate = _geometry("polygon", actual) if len(actual) >= 3 else None
        cells = _polygon_cells(actual) if candidate else None
        _draft_frame(row, row["frames"][item["after_frame_ordinal"] - 1], before, document_id,
                     "polygon", operation, candidate, cells)
    _draft(row, prefix + ".opened", before, document_id, "polygon", operation,
           start=prefix + ".vertex.1.press")
    candidate = _geometry("polygon", actual) if count >= 3 else None
    cells = _polygon_cells(actual) if candidate else None
    _draft(row, prefix + ".preview", before, document_id, "polygon", operation, candidate, cells,
           start=prefix + ".vertex.1.press")
    return actual


def _first_cases(row, case_id):
    initial, _, _ = _state(row, "initial")
    if case_id == "S01":
        _basic(row, "replace", initial, "replace", (18, 6), ((21, 8), (25, 10)), ((18, 6, 3, 2), (18, 6, 7, 4)))
    elif case_id in {"S02", "S04"}:
        _basic(row, "add" if case_id == "S02" else "shift_alt", initial, "add", (18, 6),
               ((21, 8), (25, 10)), ((18, 6, 4, 4), (18, 6, 8, 8)),
               modifiers=("Shift",) if case_id == "S02" else ("Shift", "Alt"))
    elif case_id == "S03":
        _basic(row, "subtract", initial, "subtract", (8, 7), ((10, 8), (12, 10)),
               ((8, 7, 2, 1), (8, 7, 4, 3)), modifiers=("Alt",))
    elif case_id == "S05":
        for prefix, before, operation, opened_mods, first_box, last_box in (
            ("early", initial, "add", ("Shift",), (18, 6, 4, 4), (18, 6, 7, 4)),
            ("late", _api(row, "base_reset_preparation")["after"], "replace", (), (18, 6, 3, 2), (18, 6, 8, 8))):
            _pointer(row, prefix + ".press", (18, 6), "press", opened_mods)
            _draft(row, prefix + ".opened", before, row["source_document_id"], "rectangle", operation, start=prefix + ".press")
            first, last = _box_geometry("rectangle", first_box), _box_geometry("rectangle", last_box)
            _pointer(row, prefix + ".drag.1", (21, 8), "drag", opened_mods)
            _draft_frame(row, _frame(row, prefix + ".drag.1"), before, row["source_document_id"], "rectangle", operation, first, _rect(first_box))
            latch_label = "early.key_released" if prefix == "early" else "late.key_pressed"
            _draft(row, latch_label, before, row["source_document_id"], "rectangle", operation, first, _rect(first_box))
            latch_frame = _frame(row, "early.shift.up" if prefix == "early" else "late.shift.down")
            assert latch_frame["requested_events"] == [{"type": "keys", "keys": [{"name": "Shift", "down": prefix == "late"}]}]
            if prefix == "early":
                _draft(row, "early.preview", before, row["source_document_id"], "rectangle", "add", first, _rect(first_box))
                assert _state(row, "early.preview")[0]["frame_ordinal"] < latch_frame["ordinal"]
            _pointer(row, prefix + ".drag.2", (25, 10), "drag", () if prefix == "early" else ("Shift",))
            _draft_frame(row, _frame(row, prefix + ".drag.2"), before, row["source_document_id"], "rectangle", operation, last, _rect(last_box))
            if prefix == "late":
                _draft(row, "late.preview", before, row["source_document_id"], "rectangle", operation, last, _rect(last_box))
            after, rendered, _ = _state(row, prefix + ".published")
            _pointer(row, prefix + ".release", (25, 10), "release", () if prefix == "early" else ("Shift",))
            _resources(rendered["selection_creation"], False)
            result_cells = BASE_CELLS | _rect(last_box) if operation == "add" else _rect(last_box)
            _published(before, after, row["source_document_id"], _mask(result_cells) if operation == "add" else last,
                       result_cells, candidate=last, operation=operation)
        preparation = _api(row, "base_reset_preparation")
        _selection(_owner(preparation["after"], row["source_document_id"]), BASE, BASE_CELLS)
    elif case_id == "S06":
        _selectors(row, "selection_creation.mode.intersect")
        first = _basic(row, "intersect.overlap", initial, "intersect", (8, 7), ((18, 15),), ((8, 7, 10, 8),))
        _basic(row, "intersect.empty", first, "intersect", (22, 16), ((28, 20),), ((22, 16, 6, 4),))
        _selection(_state(row, "intersect.empty.published")[2], _mask(frozenset()), frozenset())
    elif case_id == "S07":
        _selectors(row, "selection_creation.mode.xor")
        first = _basic(row, "xor.hole", initial, "xor", (8, 7), ((12, 10),), ((8, 7, 4, 3),))
        _basic(row, "xor.island", first, "xor", (20, 15), ((25, 20),), ((20, 15, 5, 5),))
    elif case_id == "S08":
        first = _basic(row, "ellipse.add", initial, "add", (18, 6), ((21, 8), (25, 10)),
                       ((17, 5, 4, 4), (17, 4, 8, 8)), modifiers=("Shift",), tool="ellipse")
        _selectors(row, "selection_creation.mode.replace")
        _basic(row, "ellipse.replace", first, "replace", (22, 15), ((29, 20),), ((22, 15, 7, 5),), tool="ellipse")
    elif case_id == "S09":
        points = []
        for index, wanted in enumerate(((18, 4), (25, 4), (25, 11), (18, 11)), 1):
            mods = ("Shift",) if index == 1 else ("Alt",) if index == 3 else ()
            item, actual = _pointer(row, f"polygon.vertex.{index}.press", wanted, "press", mods)
            points.append(actual)
            _pointer(row, f"polygon.vertex.{index}.release", wanted, "release", mods)
            candidate = _geometry("polygon", points) if index >= 3 else None
            cells = _polygon_cells(points) if candidate else None
            _draft_frame(row, row["frames"][item["after_frame_ordinal"] - 1], initial, row["source_document_id"], "polygon", "add", candidate, cells)
            label = ("polygon.opened", "polygon.vertex2", "polygon.vertex3", "polygon.preview")[index - 1]
            _draft(row, label, initial, row["source_document_id"], "polygon", "add", candidate, cells, start="polygon.vertex.1.press")
        item, final = _pointer(row, "polygon.finish.press", (18, 4), "press", ())
        first = _pointer(row, "polygon.vertex.1.press")[0]
        assert item["delivered_screen_point"] == first["delivered_screen_point"] and final == points[0]
        _pointer(row, "polygon.finish.release", (18, 4), "release", ())
        candidate, cells = _geometry("polygon", points), _polygon_cells(points)
        after, rendered, _ = _state(row, "polygon.published")
        _published(initial, after, row["source_document_id"], _mask(BASE_CELLS | cells), BASE_CELLS | cells,
                   candidate=candidate, operation="add")
        _resources(rendered["selection_creation"], False)
    elif case_id == "S10":
        _pointer(row, "freehand.press", (8, 7), "press", ("Alt",))
        _draft(row, "freehand.opened", initial, row["source_document_id"], "freehand", "subtract", start="freehand.press")
        points = [_pointer(row, "freehand.press")[1]]
        for index, wanted in enumerate(((12, 7), (12, 10), (8, 10), (8, 7)), 1):
            item, actual = _pointer(row, f"freehand.drag.{index}", wanted, "drag", ("Alt",))
            points.append(actual)
            candidate = _geometry("freehand", points) if index >= 2 else None
            cells = _polygon_cells(points) if candidate else None
            _draft_frame(row, row["frames"][item["after_frame_ordinal"] - 1], initial, row["source_document_id"], "freehand", "subtract", candidate, cells)
        candidate, cells = _geometry("freehand", points), _polygon_cells(points)
        _draft(row, "freehand.preview", initial, row["source_document_id"], "freehand", "subtract", candidate, cells)
        _pointer(row, "freehand.release", (8, 7), "release", ("Alt",))
        after, rendered, _ = _state(row, "freehand.published")
        _published(initial, after, row["source_document_id"], _mask(BASE_CELLS - cells), BASE_CELLS - cells,
                   candidate=candidate, operation="subtract")
        _resources(rendered["selection_creation"], False)


def _lifecycle_cases(row, case_id):
    initial, _, source = _state(row, "initial")
    source_id = row["source_document_id"]
    if case_id == "S11":
        _pointer(row, "escape.press", (18, 6), "press", ("Shift",))
        _draft(row, "escape.opened", initial, source_id, "rectangle", "add", start="escape.press")
        _pointer(row, "escape.drag", (25, 10), "drag", ("Shift",))
        _draft(row, "escape.preview", initial, source_id, "rectangle", "add",
               _box_geometry("rectangle", (18, 6, 8, 8)), _rect((18, 6, 8, 8)))
        _pointer(row, "escape.release", (25, 10), "release", ("Shift",))
        _cancelled(row, "escape.cancelled", initial, reason="escape")
        for prefix, count, selector, reason in (("tool", 2, "tools.tool.ellipse", "tool_changed"),
                                                 ("mode", 3, "selection_creation.mode.xor", "mode_changed")):
            _polygon(row, prefix, initial, count)
            _selectors(row, selector)
            cause = _cancellation_boundary(row, prefix + ".preview", prefix + ".cancelled", {selector}, semantic=reason)
            _cancelled(row, prefix + ".cancelled", initial, reason=cause)
            assert _frame(row, prefix + ".later_enter.down")["requested_events"] == [
                {"type": "keys", "keys": [{"name": "Enter", "down": True}]}]
        for label in ("escape.cancel.down", "escape.later_enter.down"):
            name = "Escape" if label.startswith("escape.cancel") else "Enter"
            assert _frame(row, label)["requested_events"] == [{"type": "keys", "keys": [{"name": name, "down": True}]}]
    elif case_id == "S12":
        other_id = row["fixture_sources"]["other"]["document_id"]
        _polygon(row, "owner", initial)
        switched, rendered, _ = _state(row, "owner.switched", other_id)
        assert switched["active_document_id"] == other_id
        assert switched["workflow"] == switched["results"] == [] and switched["recording"] is True
        for document in initial["documents"]:
            _document_unchanged(document, _owner(switched, document["document_id"]))
        _resources(rendered["selection_creation"], False)
        cause = _cancellation_boundary(row, "owner.preview", "owner.switched", {f"document.{other_id}"})
        assert rendered["selection_creation"]["last_cancel"] == cause
        _cancelled(row, "owner.returned", initial, reason=cause)
        _polygon(row, "owner.close", initial)
        _draft(row, "owner.closed_draft", initial, source_id, "polygon", "replace",
               _geometry("polygon", [_pointer(row, f"owner.close.vertex.{i}.press")[1] for i in range(1, 4)]),
               _polygon_cells([_pointer(row, f"owner.close.vertex.{i}.press")[1] for i in range(1, 4)]))
        closed, rendered, _ = _state(row, "owner.closed", other_id)
        assert closed["document_ids"] == closed["facade_ids"] == [other_id] and closed["active_document_id"] == other_id
        _document_unchanged(source, row["detached_sources"]["closed_source"])
        _document_unchanged(_owner(initial, other_id), _owner(closed, other_id))
        assert closed["workflow"] == closed["results"] == []
        _resources(rendered["selection_creation"], False)
        cause = _cancellation_boundary(row, "owner.close.preview", "owner.closed", {"menu.File", "menu.File.Close_image"})
        assert rendered["selection_creation"]["last_cancel"] == cause
        _selectors(row, f"document.{other_id}", f"document.{source_id}", "menu.File.Close_image")
    elif case_id == "S13":
        preparation = _api(row, "exact_noop_base_preparation")
        before, _, prepared = _state(row, "noop.prepared")
        _collection_unchanged(preparation["after"], before)
        _selection(prepared, _mask(BASE_CELLS), BASE_CELLS)
        _selection(prepared, BASE, BASE_CELLS, previous=True)
        _document_unchanged(source, prepared, selection=False)
        assert prepared["revisions"]["selection"] == source["revisions"]["selection"] + 1
        _selectors(row, "selection_creation.mode.add")
        no_op = _basic(row, "noop", before, "add", (8, 7), ((12, 10),), ((8, 7, 4, 3),))
        _collection_unchanged(before, no_op)
        _pointer(row, "zero.press", (18, 6), "press", ())
        _draft(row, "zero.opened", no_op, source_id, "rectangle", "add", start="zero.press")
        _pointer(row, "zero.release", (18, 6), "release", ())
        zero, rendered, _ = _state(row, "zero.published")
        _collection_unchanged(no_op, zero)
        _resources(rendered["selection_creation"], False)
    elif case_id == "S14":
        preparation = _api(row, "queued_apply_preparation")
        jobs = preparation["jobs"]["gaussian_jobs"]
        assert jobs["apply"]["owns_job"] is True and jobs["apply"]["job"]["state"] == "queued"
        assert jobs["apply"]["source_id"] == source_id
        busy, rendered, _ = _state(row, "busy.blocked")
        assert busy["busy"] is True
        _collection_unchanged(initial, busy)
        _resources(rendered["selection_creation"], False)
        assert rendered["selection_creation"]["admission"] == "busy"
        _selectors(row, "selection_creation.mode.add", disabled=True)
        for prefix in ("busy", "focus"):
            _pointer(row, prefix + ".press", (18, 6), "press", ())
            _pointer(row, prefix + ".drag", (25, 10), "drag", ())
            _pointer(row, prefix + ".release", (25, 10), "release", ())
        focus, rendered, _ = _state(row, "focus.blocked")
        _collection_unchanged(initial, focus)
        assert focus["busy"] is False
        _resources(rendered["selection_creation"], False)
        assert rendered["file_browser"]["open"] is True
        assert rendered["selection_creation"]["admission"] == "browser"
        path_clicks = [item for item in row["control_observations"] if item["selector"] == "browser.path"]
        assert len(path_clicks) == 1
        focused_frame = row["frames"][path_clicks[0]["before_frame_ordinal"] + 2]
        assert focused_frame["rendered"]["input_frame"]["focus"]["active_id"] == path_clicks[0]["control"]["id"]
        assert focused_frame["rendered"]["input_frame"]["input_text"]["id"] == path_clicks[0]["control"]["id"]
        _selectors(row, "menu.file.open", "browser.path", "browser.cancel")
        _polygon(row, "plane", initial)
        wheel = _frame(row, "plane.wheel")
        assert wheel["ordinal"] == row["states"]["plane.preview"]["frame_ordinal"] + 1
        assert row["states"]["plane.cancelled"]["frame_ordinal"] == wheel["ordinal"] + 1
        assert wheel["requested_events"] == [{"type": "wheel", "x": 0, "y": -1}]
        assert wheel["rendered"]["input_frame"]["popup_stack"] == []
        assert wheel["rendered"]["input_frame"]["mouse_down"] == [False] * 3
        assert wheel["rendered"]["orthogonal_navigation"]["ordinary_canvas_focused"] is True
        assert wheel["rendered"]["selection_creation"]["admission"] == "admitted"
        assert _owner(wheel["native_state"], source_id)["display"]["crosshair"] == [16, 12, 0]
        _resources(wheel["rendered"]["selection_creation"], False)
        assert wheel["rendered"]["selection_creation"]["last_cancel"] == "position_changed"
        plane, rendered, changed = _state(row, "plane.cancelled")
        _collection_unchanged(initial, plane, display=False)
        assert changed["display"]["crosshair"] == [16, 12, 0]
        assert changed["display"]["t"] == changed["display"]["c"] == 0
        assert {key: value for key, value in changed["display"].items() if key not in {"crosshair", "revision"}} == {
            key: value for key, value in source["display"].items() if key not in {"crosshair", "revision"}}
        assert changed["revisions"]["display"] == source["revisions"]["display"] + 1
        _resources(rendered["selection_creation"], False)
        assert rendered["selection_creation"]["last_cancel"] == "position_changed"
        for name in ("busy_apply_refusal", "stale_source_refusal", "foreign_owner_refusal", "invalid_candidate_refusal",
                     "working_cap_refusal", "stale_metadata_refusal", "closed_source_refusal"):
            _api(row, name, refused=True)
        stale = _api(row, "stale_selection_preparation")
        first, last = _owner(stale["before"], source_id), _owner(stale["after"], source_id)
        _document_unchanged(first, last, selection=False)
        _selection(last, SMALL, SMALL_CELLS)
        assert last["revisions"]["selection"] == first["revisions"]["selection"] + 1
        metadata = _api(row, "metadata_preparation")
        first, last = _owner(metadata["before"], source_id), _owner(metadata["after"], source_id)
        assert last["metadata"]["properties"] == dict(first["metadata"]["properties"], stale=True)
        assert last["revisions"]["metadata"] == first["revisions"]["metadata"] + 1
        assert last["revisions"]["pixels"] == first["revisions"]["pixels"] and last["planes"] == first["planes"]
        closed = _api(row, "close_owner_preparation")
        other_id = row["fixture_sources"]["other"]["document_id"]
        assert other_id not in closed["after"]["document_ids"]
        _document_unchanged(_owner(closed["before"], other_id), row["detached_sources"]["closed_other"])
        complete, rendered, _ = _state(row, "guards.complete")
        assert complete["busy"] is False
        _resources(rendered["selection_creation"], False)


def _file_session(row, directory):
    descriptor = row["files"]["native_session"]
    assert set(descriptor) == {"member", "byte_count", "hex"}
    assert PurePosixPath(descriptor["member"]).name == descriptor["member"]
    assert descriptor["member"] == "selection-creation.ijx"
    for label in ("session.save.path.text", "session.open.path.text"):
        events = _frame(row, label)["requested_events"]
        assert len(events) == 1 and events[0]["type"] == "text"
        assert Path(events[0]["value"]).resolve(strict=True) == _member(directory, descriptor["member"])
    raw = _member(directory, descriptor["member"]).read_bytes()
    assert 0 < len(raw) == _integer(descriptor["byte_count"], 1) <= MAX_ARCHIVE and raw.hex() == descriptor["hex"]
    assert raw.startswith(b"IMAGEJXX3\n") and len(raw) > 18
    size, = struct.unpack("<Q", raw[10:18])
    assert 0 < size <= MAX_ARCHIVE
    decoder = zlib.decompressobj()
    plain = decoder.decompress(raw[18:], size + 1)
    assert len(plain) == size and decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail
    value = _decode(plain, MAX_ARCHIVE)
    assert set(value) == {"format", "version", "documents", "results", "workflow", "extensions"}
    assert value["format"] == "imagejxx-session" and type(value["version"]) is int and value["version"] == 3
    return value


def _session_selection(value):
    if value is None:
        return None
    names = {"kind", "points", "width"} | ({"shape", "mask"} if value["kind"] == "mask" else set())
    return {**{key: value[key] for key in names}, "attributes": {key: item for key, item in value.items() if key not in names}}


def _session_document(value, document):
    assert value["axes"] == "TCZYXS" and value["shape"] == document["shape"]
    assert value["pixel_type"] == document["dtype"] and value["sample_policy"] == document["sample_policy"]
    assert value["storage"] == "Float64LE"
    assert [base64.b64decode(item, validate=True).hex() for item in value["planes"]] == [plane["storage_f64_hex"] for plane in document["planes"]]
    for name in ("calibration", "metadata", "overlays"):
        assert value[name] == document[name]
    for name in ("selection", "previous_selection"):
        assert value[name] == _session_selection(document[name])
    assert value["rois"] == [dict(roi, selection=_session_selection(roi["selection"])) for roi in document["rois"]]
    assert value["fourier"] is None
    assert value["display"] == {name: item for name, item in document["display"].items() if name != "revision"}


def _recording_case(row, directory):
    source_id, other_id = row["source_document_id"], row["fixture_sources"]["other"]["document_id"]
    initial, _, source = _state(row, "initial")
    original = _basic(row, "record.original", initial, "replace", (18, 6), ((21, 8), (25, 10)),
                      ((18, 6, 3, 2), (18, 6, 7, 4)))
    candidate = _box_geometry("rectangle", (18, 6, 7, 4))
    record = row["original_record"]
    _record(record, source, candidate, candidate, "replace")
    assert row["original_owner_id"] == source_id and original["workflow"] == [record]
    record_clicks = [item for item in row["control_observations"] if item["selector"] == "workflow.record"]
    assert len(record_clicks) == 4
    first_press = _pointer(row, "record.original.press")[0]["after_frame_ordinal"]
    assert record_clicks[0]["before_frame_ordinal"] < record_clicks[1]["before_frame_ordinal"] < first_press
    # Read actual post-click collection bits, not widget labels or producer claims.
    assert row["frames"][record_clicks[0]["before_frame_ordinal"] + 2]["native_state"]["recording"] is False
    assert row["frames"][record_clicks[1]["before_frame_ordinal"] + 2]["native_state"]["recording"] is True
    preparation = _api(row, "replay_selection_preparation")
    _selection(_owner(preparation["after"], source_id), SMALL, SMALL_CELLS)
    replayed, _, replay_owner = _state(row, "record.replayed")
    _published(preparation["after"], replayed, source_id, candidate, _rect((18, 6, 7, 4)), record=record)
    _selection(replay_owner, SMALL, SMALL_CELLS, previous=True)
    destination, _, other = _state(row, "rebind.destination", other_id)
    assert destination["active_document_id"] == other_id
    _document_unchanged(row["fixture_sources"]["other"], other)
    assert destination["workflow"] == [record, record]
    opened, rendered, _ = _state(row, "rebind.opened", other_id)
    _collection_unchanged(destination, opened)
    assert rendered["selection_creation"]["rebind"] == {"record": record, "document_id": other_id}
    _resources(rendered["selection_creation"], False)
    _pointer(row, "rebind.press", (8, 7), "press", ())
    _pointer(row, "rebind.drag", (12, 10), "drag", ())
    hole = _box_geometry("rectangle", (8, 7, 4, 3))
    _draft(row, "rebind.preview", opened, other_id, "rectangle", "subtract", hole, HOLE_CELLS, start="rebind.press")
    _pointer(row, "rebind.release", (12, 10), "release", ())
    rebound, rendered, rebound_owner = _state(row, "rebind.published", other_id)
    result = _mask(BASE_CELLS - HOLE_CELLS)
    _published(opened, rebound, other_id, result, BASE_CELLS - HOLE_CELLS, candidate=hole, operation="subtract")
    _resources(rendered["selection_creation"], False)
    assert rendered["selection_creation"]["rebind"] is None
    assert row["rebound_record"] == rebound["workflow"][2] and row["rebind_owner_id"] == other_id
    _record(row["rebound_record"], other, hole, result, "subtract")
    assert row["rebound_record"]["source"]["id"] != record["source"]["id"]
    cancelled_opened, rendered, _ = _state(row, "cancel_rebind.opened", other_id)
    _collection_unchanged(rebound, cancelled_opened)
    assert rendered["selection_creation"]["rebind"] == {"record": record, "document_id": other_id}
    cancelled, rendered, _ = _state(row, "cancel_rebind.cancelled", other_id)
    _collection_unchanged(rebound, cancelled)
    assert rendered["selection_creation"]["rebind"] is None
    _resources(rendered["selection_creation"], False)
    off_preparation = _api(row, "rebind_recording_off_selection_preparation")
    assert off_preparation["before"]["recording"] is off_preparation["after"]["recording"] is False
    _selection(_owner(off_preparation["after"], other_id), SMALL, SMALL_CELLS)
    off, _, off_owner = _state(row, "record.off_replayed", other_id)
    _published(off_preparation["after"], off, other_id, result, BASE_CELLS - HOLE_CELLS, record=row["rebound_record"])
    _selection(off_owner, SMALL, SMALL_CELLS, previous=True)
    assert off["recording"] is False and off["workflow"] == rebound["workflow"] and row["recording_off_workflow_rows"] == 3
    saved, _, saved_owner = _state(row, "saved", other_id)
    assert saved["recording"] is True and saved["workflow"] == row["saved_workflow"] == [record, record, row["rebound_record"]]
    assert row["saved_owner_id"] == other_id
    root = _file_session(row, directory)
    assert len(root["documents"]) == 1 and root["workflow"] == saved["workflow"] and root["results"] == []
    _session_document(root["documents"][0], saved_owner)
    reopened_id = _integer(row["reopened_owner_id"], 1)
    assert reopened_id not in initial["document_ids"]
    reopened, _, reopened_owner = _state(row, "reopened", reopened_id)
    assert reopened["active_document_id"] == reopened_id and reopened["workflow"] == saved["workflow"]
    _literal(reopened_owner)
    for key in ("selection", "previous_selection", "selection_observation", "previous_selection_observation",
                "calibration", "metadata", "rois", "overlays", "spectrum_present"):
        assert reopened_owner[key] == saved_owner[key], key
    assert [item["source"]["id"] for item in reopened["workflow"]] == [source_id, source_id, other_id]
    malformed = _api(row, "workflow_malformed_preparation")
    assert malformed["after"]["workflow"] == {"malformed": True}
    _pointer(row, "malformed.press", (18, 6), "press", ())
    _draft(row, "malformed.opened", malformed["after"], reopened_id, "rectangle", "replace", start="malformed.press")
    for index, point, box in ((1, (21, 8), (18, 6, 3, 2)), (2, (25, 10), (18, 6, 7, 4))):
        _pointer(row, f"malformed.drag.{index}", point, "drag", ())
        _draft_frame(row, _frame(row, f"malformed.drag.{index}"), malformed["after"], reopened_id,
                     "rectangle", "replace", _box_geometry("rectangle", box), _rect(box))
    _draft(row, "malformed.preview", malformed["after"], reopened_id, "rectangle", "replace", candidate, _rect((18, 6, 7, 4)))
    _pointer(row, "malformed.release", (25, 10), "release", ())
    refused, rendered, _ = _state(row, "malformed.refused", reopened_id)
    _collection_unchanged(malformed["after"], refused)
    assert rendered["error_modal"]["open"] is True and rendered["error_modal"]["message"]
    _resources(rendered["selection_creation"], False)
    restored = _api(row, "workflow_restore_preparation")
    assert restored["after"]["workflow"] == saved["workflow"]
    _selectors(row, "workflow.record", "workflow.selection_creation.0.replay", "workflow.selection_creation.0.rebind",
               "workflow.selection_creation.2.replay", "selection_creation.rebind.cancel", "selection_creation.mode.subtract",
               "menu.path.save_document", "menu.file.open", "browser.path", "browser.submit", "error_modal.ok")


def _unsupported_case(row):
    source_id, scientific_id = row["source_document_id"], row["fixture_sources"]["scientific"]["document_id"]
    for prefix, modifiers, boxes in (
        ("absent_shift", ("Shift",), ((10, 7, 4, 4), (10, 7, 8, 8))),
        ("absent_alt", ("Alt",), ((10, 7, 3, 2), (10, 7, 7, 4)))):
        preparation = _api(row, prefix + "_preparation")
        _selection(_owner(preparation["after"], source_id), None)
        _basic(row, prefix, preparation["after"], "replace", (10, 7), ((13, 9), (17, 11)), boxes, modifiers=modifiers)
    nonarea = _api(row, "nonarea_base_preparation")
    line = _geometry("line", ((4, 4), (14, 12)))
    _selection(_owner(nonarea["after"], source_id), line)
    for label, action, point in (("nonarea.press", "press", (18, 6)), ("nonarea.drag.1", "drag", (21, 8)),
                                 ("nonarea.drag.2", "drag", (25, 10)), ("nonarea.release", "release", (25, 10))):
        _pointer(row, label, point, action, ("Shift",))
    refused, rendered, _ = _state(row, "nonarea.refused")
    _collection_unchanged(nonarea["after"], refused)
    assert rendered["error_modal"]["open"] is True and rendered["error_modal"]["message"]
    _resources(rendered["selection_creation"], False)
    replaced = _basic(row, "nonarea.replaced", refused, "replace", (18, 6), ((21, 8), (25, 10)),
                      ((18, 6, 3, 2), (18, 6, 7, 4)))
    _pointer(row, "edit.press", (21, 8), "press", ("Shift",))
    _, start = _pointer(row, "edit.press")
    _pointer(row, "edit.drag", (23, 11), "drag", ("Shift",))
    _, end = _pointer(row, "edit.drag")
    delta = end[1] - start[1]
    assert 2.5 < delta < 3.5
    # Existing edit-ROI Shift compares physical dx with dy*aspectY. It
    # suppresses X on this path (2 versus3*3), retaining continuous Y.
    assert abs(end[0] - start[0]) < abs(delta * 3)
    expected = _geometry("rectangle", ((18, 6 + delta), (25, 10 + delta)))
    preview, rendered, _ = _state(row, "edit.preview")
    _collection_unchanged(replaced, preview)
    _resources(rendered["selection_creation"], False)
    _pointer(row, "edit.release", (23, 11), "release", ("Shift",))
    edited, rendered, _ = _state(row, "edit.published")
    # Pinned Roi.java182-186 truncates the positive double origin8.981... to8
    # and independently ceils the four-pixel span. Continuous geometry above
    # remains exact; its covered rows are8..11 rather than rounded9..12.
    _published(replaced, edited, source_id, expected, _rect((18, 8, 7, 4)), recording=False)
    _resources(rendered["selection_creation"], False)
    scientific_inputs = {}
    for label, point, action in (("scientific.press", (18, 6), "press"),
                                  ("scientific.drag", (25, 10), "drag"), ("scientific.release", (25, 10), "release")):
        scientific_inputs[action] = _pointer(row, label, point, action, ())[0]
    scientific, rendered, scientific_owner = _state(row, "scientific.guards", scientific_id)
    assert scientific["active_document_id"] == scientific_id
    original_scientific = row["fixture_sources"]["scientific"]
    _document_unchanged(original_scientific, scientific_owner, display=False)
    press, drag, release = (scientific_inputs[action] for action in ("press", "drag", "release"))
    assert drag["after_frame_ordinal"] == press["after_frame_ordinal"] + 1
    assert release["after_frame_ordinal"] == drag["after_frame_ordinal"] + 1
    assert press["control"]["scale"] == drag["control"]["scale"] == release["control"]["scale"]
    assert press["control"]["origin"] == drag["control"]["origin"] == release["control"]["origin"]
    # Pre-P05l Window canvas5677/6111-6114/6184-6194 uses effective Hand on
    # scientific documents. Its delivered screen delta pans the view and pins
    # the current fitted float zoom as world-units plane_zoom[0]. This remains
    # navigation while creation admission stays scientific_read_only.
    screen_delta = [_f32(_f32(drag["delivered_screen_point"][axis]) -
                         _f32(press["delivered_screen_point"][axis])) for axis in range(2)]
    assert any(screen_delta)
    expected_display = copy.deepcopy(original_scientific["display"])
    assert expected_display["linked_zoom"] is False
    expected_display["pan"][0] = [_f32(_f32(expected_display["pan"][0][axis]) + screen_delta[axis]) for axis in range(2)]
    captured_zoom = press["control"]["scale"][0]
    assert _f32(captured_zoom) == captured_zoom
    expected_display["plane_zoom"][0] = captured_zoom / CALIBRATION["spacing"][0]
    expected_display["revision"] += 1
    assert _typed(scientific_owner["display"]) == _typed(expected_display)
    assert scientific_owner["revisions"]["display"] == original_scientific["revisions"]["display"] + 1
    for frame in row["frames"][press["before_frame_ordinal"] - 1:scientific["frame_ordinal"]]:
        state, diagnostic = frame["native_state"], frame["rendered"]["selection_creation"]
        current = _owner(state, scientific_id)
        _document_unchanged({key: original_scientific[key] for key in current}, current, display=False)
        panned = frame["ordinal"] >= drag["after_frame_ordinal"]
        assert _typed(current["display"]) == _typed(expected_display if panned else original_scientific["display"])
        assert current["revisions"]["display"] == original_scientific["revisions"]["display"] + int(panned)
        assert state["active_document_id"] == scientific_id and state["busy"] is False
        assert state["document_ids"] == edited["document_ids"] and state["facade_ids"] == edited["facade_ids"]
        for key in ("workflow", "results", "recording"):
            assert _typed(state[key]) == _typed(edited[key]), key
        eager = _owner(state, source_id)
        _document_unchanged({key: _owner(edited, source_id)[key] for key in eager}, eager)
        _resources(diagnostic, False)
        assert diagnostic["admission"] == "scientific_read_only" and diagnostic["rebind"] is None
        assert diagnostic["mode"] == "replace"
        for selector in ("tools.tool.rectangle", "selection_creation.mode.add"):
            assert _control(frame["rendered"], selector)["enabled"] is False
        hand = _control(frame["rendered"], "tools.tool.hand")
        assert _integer(hand["id"], 1) and hand["enabled"] is True
        if frame["ordinal"] in {press["after_frame_ordinal"], drag["after_frame_ordinal"]}:
            assert frame["rendered"]["input_frame"]["focus"]["active_id"] == press["control"]["id"]
        if frame["ordinal"] >= release["after_frame_ordinal"]:
            canvas = frame["rendered"]["ordinary_canvas"]
            assert canvas["scale"] == press["control"]["scale"] and canvas["extent"] == press["control"]["extent"]
            assert canvas["origin"] == [_f32(press["control"]["origin"][axis] + screen_delta[axis]) for axis in range(2)]
    _document_unchanged(_owner(edited, source_id), _owner(scientific, source_id))
    assert scientific["workflow"] == edited["workflow"] and scientific["results"] == edited["results"]
    _resources(rendered["selection_creation"], False)
    assert rendered["selection_creation"]["admission"] == "scientific_read_only"
    _selectors(row, "tools.tool.rectangle", "selection_creation.mode.add", disabled=True)
    _selectors(row, "tools.tool.edit_roi", "selection_creation.mode.replace", "error_modal.ok")
    _api(row, "scientific_apply_refusal", refused=True)


def _gesture_membership(row):
    checked = row.pop("_checked_gesture_labels", set())
    observations = row["gesture_observations"]
    by_label = {item["label"]: item for item in observations}
    assert len(by_label) == len(observations)
    # A press's separate physical positioning frame is already constrained by
    # its exact paired button frame, desired coordinates and original canvas.
    for label, item in by_label.items():
        if label.endswith(".position"):
            button = label.removesuffix(".position")
            if button in checked:
                paired = by_label[button]
                assert item["action"] == "position" and paired["action"] == "press"
                assert item["after_frame_ordinal"] == paired["before_frame_ordinal"]
                for key in ("requested_image_point", "delivered_screen_point"):
                    assert item[key] == paired[key]
                checked.add(label)
    assert set(by_label) == checked, f"Unexpected/unasserted canvas inputs: {set(by_label) - checked}"


def assert_observations(row, case_id, context, directory):
    """Callable independent reader for retained reports; no product imports."""
    assert case_id in CASES and context in CONTEXTS
    assert row["contract_id"] == CONTRACT and row["schema_version"] == 1
    assert type(row["schema_version"]) is int
    assert row["case_id"] == case_id and row["context"] == context
    assert row["acceptance_claim"] is False and row["observation_state"] == "observed"
    assert "failure" not in row
    assert row["fixture_recipe"] == "7*z+3*y+x"
    assert row["limits"] == {"max_frames": 180, "max_seconds": 15,
        "max_report_bytes": MAX_REPORT, "max_archive_bytes": MAX_ARCHIVE,
        "max_documents": 16, "max_shape_tczyxs": [2, 2, 2, 24, 32, 1]}
    assert 0 <= _number(row["elapsed_seconds"]) <= 15
    assert set(row["states"]) == {"initial", "final", *STATES[case_id].split()}
    names = [item["name"] for item in row["api_observations"]]
    expected_api = {"fixed_fixture_preparation", *APIS.get(case_id, "").split()}
    assert set(names) == expected_api and len(names) == len(expected_api)
    fixture = _api(row, "fixed_fixture_preparation")
    assert fixture["methods"] == ["Window.addNativeDocument", "Window.setNativeDisplay",
        "DocumentTransaction.set_property", "Document.mark_saved", "Document.undo", "Window.recording"]
    assert set(row["files"]) == ({"native_session"} if case_id == "S15" else set())
    assert set(row["detached_sources"]) == ({"closed_source"} if case_id == "S12" else
                                           {"closed_other"} if case_id == "S14" else set())
    _physical(row)
    _fixtures(row)
    # Every complete state carries actual source storage. None of the owned
    # gestures may alter any source sample, including untargeted stack planes.
    for state in row["states"].values():
        assert len(state["documents"]) <= 16 and state["results"] == []
        for document in state["documents"]:
            _literal(document, document["shape"][0] == 2)
    if case_id in {f"S{i:02d}" for i in range(1, 11)}:
        _first_cases(row, case_id)
    elif case_id in {"S11", "S12", "S13", "S14"}:
        _lifecycle_cases(row, case_id)
    elif case_id == "S15":
        _recording_case(row, directory)
    else:
        _unsupported_case(row)
    _gesture_membership(row)
    final = row["states"]["final"]
    _collection_unchanged(row["states"][STATES[case_id].split()[-1]], final)
    final_frame = row["frames"][_integer(final["frame_ordinal"], 1) - 1]
    assert final_frame["label"] == "final"
    _resources(final_frame["rendered"]["selection_creation"], False)
    assert final_frame["rendered"]["selection_creation"]["rebind"] is None
    assert final["busy"] is False and final["results"] == []
    zero = {"running": 0, "queued": 0, "running_bytes": 0, "retained_bytes": 0, "reserved_bytes": 0}
    assert row["window_error"] == final["window_error"] == ""
    assert row["final_usage"] == final["usage"] == zero
    assert all(not file.is_symlink() for file in directory.rglob("*"))
    files = [file for file in directory.rglob("*") if file.is_file()]
    assert sum(file.stat().st_size for file in files) <= MAX_ARCHIVE


@pytest.mark.parametrize("context", CONTEXTS)
@pytest.mark.parametrize("case_id", CASES)
def test_installed_selection_creation(case_id, context, tmp_path, request):
    directory = tmp_path / f"{case_id}-{context}-observations"
    row, retained = _observe(case_id, context, directory, request)
    assert_observations(row, case_id, context, retained)

