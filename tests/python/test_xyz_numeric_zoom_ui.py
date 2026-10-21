"""Independent P05d arithmetic/raw assertions; stdlib only, no product imports.

These functions consume real observations supplied by the future producer. They
create no runtime reports, execute no gate, and grant no acceptance credit.
"""
from decimal import Decimal
import hashlib
import math
import struct

SHAPE_ZYX = (9, 17, 32)
SPACING_XYZ = (0.4, 1.2, 3.0)
INITIAL_XYZ = (7, 5, 3)
BASE_VIEW = {"orthogonal": True, "linked_zoom": False, "zoom": 1.25,
             "plane_zoom": [2.0, 3.0, 4.0],
             "pan": [[3.25, -2.5], [5.5, -4.25], [7.75, -6.5]],
             "crosshair": [7, 5, 3]}
PLAN_SOURCE_SHA256 = "b3e0f65e2f86dd2790085a5e7023ed9a336258c7bfb73e5a1820509ecc1ae51c"


def float32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def float32_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def double_bits(value):
    return struct.unpack("<Q", struct.pack("<d", value))[0]


def pixel(x, y, z, edited_z=None):
    return 10000 * z + 100 * y + x + (1 if z == edited_z else 0)


def source_raw(edited_z=None):
    return b"".join(struct.pack("<f", pixel(x, y, z, edited_z))
                    for z in range(9) for y in range(17) for x in range(32))


def plane_raw(plane, xyz=INITIAL_XYZ, edited_z=None):
    x, y, z = xyz
    if plane == "XY":
        values = (pixel(a, b, z, edited_z) for b in range(17) for a in range(32))
    elif plane == "XZ":
        values = (pixel(a, y, b, edited_z) for b in range(9) for a in range(32))
    elif plane == "YZ":
        values = (pixel(x, a, b, edited_z) for b in range(9) for a in range(17))
    else:
        raise ValueError("Unknown scientific plane")
    return b"".join(struct.pack("<f", value) for value in values)


def world_xyz(xyz):
    x, y, z = map(Decimal, xyz)
    return (Decimal(10) - Decimal("1.2") * y,
            Decimal(-5) + Decimal("0.4") * x,
            Decimal(2) + Decimal(3) * z)


def physical_zoom(percent, plane):
    # Declared UI arithmetic: field is binary32; divide by100 in binary32,
    # then widen and divide by the selected binary64 calibration spacing.
    field = float32(float(percent))
    if not math.isfinite(field):
        raise ValueError("Finite percentage required")
    bounded = min(max(field, float32(0.01)), float32(6400.0))
    fraction = float32(bounded / float32(100.0))
    axis = {"XY": 0, "XZ": 0, "YZ": 1}[plane]
    return float(fraction) / SPACING_XYZ[axis]


def expected_view(percent, plane, linked=False, previous=None):
    initial = BASE_VIEW if previous is None else previous
    view = {key: ([list(x) for x in value] if key == "pan" else list(value)
                  if isinstance(value, list) else value)
            for key, value in initial.items()}
    view["linked_zoom"] = linked
    value = physical_zoom(percent, plane)
    if linked:
        view["zoom"] = value
    else:
        view["plane_zoom"][("XY", "XZ", "YZ").index(plane)] = value
    return view


def render_scales(view):
    axes = ((0, 1), (0, 2), (1, 2))
    return {plane: tuple(float32((view["zoom"] if view["linked_zoom"] else
                                  view["plane_zoom"][index]) * SPACING_XYZ[axis])
                         for axis in axes[index])
            for index, plane in enumerate(("XY", "XZ", "YZ"))}


def assert_view(actual, expected):
    for key in ("orthogonal", "linked_zoom", "crosshair", "pan"):
        assert actual[key] == expected[key], key
    for key in ("zoom",):
        assert double_bits(actual[key]) == double_bits(expected[key]), key
    assert [double_bits(x) for x in actual["plane_zoom"]] == [
        double_bits(x) for x in expected["plane_zoom"]]


def assert_scales(actual_scales, expected):
    for plane, pair in render_scales(expected).items():
        # Reports serialize actual binary32 scale values as exact JSON doubles.
        assert tuple(actual_scales[plane]) == pair, plane
        assert tuple(float32_bits(x) for x in actual_scales[plane]) == tuple(
            float32_bits(x) for x in pair), plane


def assert_world(actual, xyz=INITIAL_XYZ):
    expected = world_xyz(xyz)
    assert len(actual) == 3
    assert all(math.isfinite(x) for x in actual)
    assert all(abs(Decimal.from_float(float(x)) - y) <= Decimal("1e-12")
               for x, y in zip(actual, expected))


def assert_raw(actual, plane=None, xyz=INITIAL_XYZ, edited_z=None):
    expected = source_raw(edited_z) if plane is None else plane_raw(plane, xyz, edited_z)
    assert bytes(actual) == expected
    return hashlib.sha256(expected).hexdigest()


def assert_atomic(before, after):
    # Caller supplies actual normalized complete native/settings/workflow state.
    # Invalid commit must not advance even the display revision.
    assert after == before


def assert_camera_only(before, after):
    # Display is the only permitted revision category for accepted UI changes.
    for key, value in before.items():
        if key not in ("display", "display_revision", "view_revision"):
            assert after[key] == value, key
    assert after["display_revision"] > before["display_revision"]


def assert_history_delta(before, changed, undone, redone):
    assert changed["undo_steps"] == before["undo_steps"] + 1
    assert undone["undo_steps"] == before["undo_steps"]
    assert undone["redo_steps"] == changed["redo_steps"] + 1
    assert redone["undo_steps"] == changed["undo_steps"]
    assert redone["redo_steps"] == changed["redo_steps"]


# Installed reader protocol. The preceding arithmetic is the immutable owned
# stdlib oracle; native observations and physical inputs are checked below.
import importlib
import json
import os
from pathlib import Path
import re

import pytest

CONTRACT_SHA = "3b4c1840d00a773da7b3e5e7fffba08c2292dce00b395ab0dc18da124137480b"
PREFIX = "P05d.xyz_numeric_zoom."
VARIANTS = [PREFIX + value for value in (
    "independent_xz150", "independent_yz60", "linked_yz120",
    "nonfinite_commit_atomic", "numeric_focus_ownership", "session_and_pixel_undo")]
CONTEXTS = ["imgui_cpp", "imgui_python"]
CALIBRATION = {"spacing": [0.4, 1.2, 3.0], "origin": [10, -5, 2],
               "direction": [0, -1, 0, 1, 0, 0, 0, 0, 1], "unit": "um",
               "frame_interval": 1, "time_origin": 0, "time_unit": "s"}


def no_constant(value):
    raise ValueError("Nonfinite JSON constant: " + value)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, "Duplicate JSON key: " + key
        result[key] = value
    return result


def decode(value):
    result = json.loads(value, parse_constant=no_constant, object_pairs_hook=unique_object)
    pending = [result]
    while pending:
        item = pending.pop()
        if type(item) is float:
            assert math.isfinite(item)
        elif type(item) is dict:
            pending.extend(item.values())
        elif type(item) is list:
            pending.extend(item)
    return result


def integer(value, positive=False):
    assert type(value) is int and value >= (1 if positive else 0)
    return value


def member_path(root, value):
    root = root.resolve(strict=True)
    path = Path(value)
    path = (path if path.is_absolute() else root / path).resolve(strict=True)
    assert path.is_relative_to(root) and path.is_file() and not path.is_symlink()
    return path


def raw_member(descriptor, root, shape):
    assert descriptor["dtype"] == "float32"
    assert descriptor["encoding"] == "IEEE754 Float32LE"
    assert descriptor["shape"] == list(shape)
    count = math.prod(shape)
    assert descriptor["sample_count"] == count and descriptor["byte_count"] == 4 * count
    assert type(descriptor["member_id"]) is str and descriptor["member_id"]
    data = member_path(root, descriptor["path"]).read_bytes()
    assert len(data) == 4 * count
    assert hashlib.sha256(data).hexdigest() == descriptor["sha256"]
    return data


def usage(value):
    for key in ("running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"):
        integer(value[key])
    assert value["reserved_bytes"] == value["running_bytes"] + value["retained_bytes"]


def camera_hex(encoded, value):
    assert type(encoded) is str
    if encoded.startswith("0x"):
        assert re.fullmatch(r"0x[0-9a-f]{16}", encoded)
        assert int(encoded[2:], 16) == double_bits(value)
    else:
        assert re.fullmatch(r"[0-9a-f]{16}", encoded)
        assert encoded == struct.pack("<d", value).hex()


def native_camera(document, expected):
    assert_view(document["display"], expected)
    bits = document["camera_double_bits"]
    camera_hex(bits["zoom"], expected["zoom"])
    assert len(bits["plane_zoom"]) == 3
    for encoded, value in zip(bits["plane_zoom"], expected["plane_zoom"]):
        camera_hex(encoded, value)


def geometry(actual, plane, xyz):
    axis = {"XY": (0, 1, 2), "XZ": (0, 2, 1), "YZ": (1, 2, 0)}[plane]
    origin = [0, 0, 0]
    origin[axis[2]] = xyz[axis[2]]
    assert_world(actual["origin"], origin)
    basis = ([0, .4, 0], [-1.2, 0, 0], [0, 0, 3])
    for field, index in (("horizontal_step", axis[0]), ("vertical_step", axis[1])):
        assert len(actual[field]) == 3
        assert all(abs(Decimal.from_float(float(a)) - Decimal(str(b))) <= Decimal("1e-12")
                   for a, b in zip(actual[field], basis[index]))
    assert actual["horizontal_axis"] == "XYZ"[axis[0]]
    assert actual["vertical_axis"] == "XYZ"[axis[1]]


def source_document(document, directory, xyz=INITIAL_XYZ, edited_z=None, overlay_position="z"):
    integer(document["document_id"], True)
    integer(document["data_identity"])
    assert document["shape_tczyxs"] == [1, 1, 9, 17, 32, 1]
    assert document["dtype"] == "float32" and type(document["dirty"]) is bool
    for key in ("undo_steps", "redo_steps"):
        integer(document[key])
    assert set(document["revisions"]) == {"pixels", "metadata", "selection", "display"}
    for value in document["revisions"].values():
        integer(value)
    assert document["calibration"] == CALIBRATION
    assert document["metadata"]["title"] == "P05d owned calibrated XYZ numeric zoom"
    assert document["metadata"]["properties"]["zoom_property"] == "preserve"
    assert document["metadata"]["extensions"]["zoom_unknown"] == "keep"
    assert len(document["rois"]) == 1
    roi = document["rois"][0]
    assert roi["name"] == "retained-zoom-roi" and (roi["t"], roi["c"], roi["z"]) == (None, None, 3)
    assert roi["attributes"]["zoom_unknown"] == "roi-kept"
    assert roi["selection"]["kind"] == "rectangle" and roi["selection"]["points"] == [[2, 3], [10, 11]]
    assert len(document["overlays"]) == 1
    overlay = document["overlays"][0]
    assert overlay["kind"] == "point" and overlay["points"] == [[7, 5]]
    assert overlay[overlay_position] == 3 and ({"z", "slice_index"} - {overlay_position}).isdisjoint(overlay)
    assert overlay["zoom_unknown"] == "overlay-kept"
    assert document["selection"] is None and document["previous_selection"] is None
    assert document["spectrum_present"] is False
    assert raw_member(document["source_raw"], directory, SHAPE_ZYX) == source_raw(edited_z)
    assert [item["plane"] for item in document["planes"]] == ["XY", "XZ", "YZ"]
    for item, shape, fixed in zip(document["planes"], ((17, 32), (9, 32), (9, 17)), (2, 1, 0)):
        assert item["coordinate"] == xyz[fixed]
        assert raw_member(item["raw"], directory, shape) == plane_raw(item["plane"], xyz, edited_z)
        geometry(item["geometry"], item["plane"], xyz)
        if "landmark_sample" in item:
            assert item["landmark_sample"] == pixel(*xyz, edited_z)
    assert document["landmark_value"] == pixel(*xyz, edited_z)
    assert_world(document["world_xyz"], xyz)


def content_document(value):
    # Content comparison strips only real artifact paths and revision-aware view
    # metadata; scientific bytes are independently checked before this function.
    result = {key: item for key, item in value.items()
              if key not in ("source_raw", "planes", "display", "camera_double_bits", "document_info")}
    result["planes"] = [{key: item for key, item in row.items() if key != "raw"}
                        for row in value["planes"]]
    return result


def unchanged_native(before, after, camera_change=False):
    original, current = content_document(before), content_document(after)
    if camera_change:
        old_revisions, new_revisions = original.pop("revisions"), current.pop("revisions")
        assert new_revisions["display"] > old_revisions["display"]
        assert {k: v for k, v in new_revisions.items() if k != "display"} == {
            k: v for k, v in old_revisions.items() if k != "display"}
    assert original == current
    # All other native display properties persist; only the declared cameras and
    # actual display revision may vary on an accepted numeric edit.
    excluded = {"zoom", "plane_zoom", "linked_zoom", "revision"} if camera_change else set()
    assert {k: v for k, v in before["display"].items() if k not in excluded} == {
        k: v for k, v in after["display"].items() if k not in excluded}
    if not camera_change:
        assert before["camera_double_bits"] == after["camera_double_bits"]


@pytest.fixture(scope="module")
def xyz_contract():
    required = os.environ.get("IMAGEJXX_REQUIRE_RENDER_TESTS") == "1"
    location = os.environ.get("IMAGEJXX_XYZ_NUMERIC_ZOOM_CONTRACT_JSON")
    if not required and not location:
        pytest.skip("Installed XYZ numeric-zoom evidence is optional outside its required gate")
    assert location, "Required adopted P05d contract missing"
    data = Path(location).resolve(strict=True).read_bytes()
    assert hashlib.sha256(data).hexdigest() == CONTRACT_SHA
    contract = decode(data)
    assert [item["id"] for item in contract["groups"]] == VARIANTS
    assert contract["group_count"] == 6 and contract["required_ui_context_rows"] == 12
    assert contract["required_API_consumers"] == 2
    assert contract["source_fixture"]["source_sha256"] == PLAN_SOURCE_SHA256
    assert hashlib.sha256(source_raw()).hexdigest() == PLAN_SOURCE_SHA256
    assert contract["groups"][4]["steps"][20]["keys"] == [
        {"name": "Shift", "down": True}, {"name": "Right", "down": True}]
    return contract


def api_python_state(application, identifier):
    snapshot = application.snapshot(identifier)
    display = snapshot.display
    data = snapshot.dataset
    cal = data.calibration
    revision = snapshot.revisions
    history = application.history(identifier)
    return {"document_id": snapshot.id, "data_identity": snapshot.data_identity,
            "dirty": snapshot.dirty, "undo_steps": history.undo_steps, "redo_steps": history.redo_steps,
            "revisions": {key: getattr(revision, key) for key in ("pixels", "metadata", "selection", "display")},
            "metadata": snapshot.metadata, "overlays": snapshot.overlays,
            "record_layout": snapshot.record_layout,
            "selection_present": snapshot.selection is not None,
            "previous_selection_present": snapshot.previous_selection is not None,
            "spectrum_present": snapshot.spectrum is not None,
            "rois": [{"name": roi.name, "t": roi.t, "c": roi.c, "z": roi.z,
                      "attributes": roi.attributes, "selection_attributes": roi.selection.attributes,
                      "kind": roi.selection.kind, "points": roi.selection.points}
                     for roi in snapshot.rois],
            "calibration": {key: getattr(cal, key) for key in CALIBRATION},
            "display": {key: getattr(display, key) for key in BASE_VIEW} | {
                "properties": display.properties, "revision": display.revision},
            "world_xyz": cal.voxel_to_world(display.crosshair),
            "geometry": {plane: data.plane_geometry(plane.lower(), coordinate)
                         for plane, coordinate in zip(("XY", "XZ", "YZ"),
                             (display.crosshair[2], display.crosshair[1], display.crosshair[0]))},
            "source_bytes": data.numpy("ZYX").tobytes(),
            "plane_bytes": {plane: data.plane(plane.lower(), coordinate=xyz).tobytes()
                            for plane, xyz in zip(("XY", "XZ", "YZ"),
                                                 (display.crosshair[2], display.crosshair[1], display.crosshair[0]))}}


def assert_python_api_state(state, expected, edited_z=None):
    integer(state["document_id"], True)
    integer(state["data_identity"])
    assert state["calibration"] == CALIBRATION
    assert not state["selection_present"] and not state["previous_selection_present"] and not state["spectrum_present"]
    assert state["metadata"]["title"] == "P05d owned calibrated XYZ numeric zoom"
    assert state["metadata"]["properties"]["zoom_property"] == "preserve"
    assert state["metadata"]["extensions"]["zoom_unknown"] == "keep"
    assert state["rois"][0]["name"] == "retained-zoom-roi"
    assert state["rois"][0]["kind"] == "rectangle" and state["rois"][0]["points"] == [[2, 3], [10, 11]]
    assert state["rois"][0]["attributes"]["zoom_unknown"] == "roi-kept"
    assert state["overlays"][0]["zoom_unknown"] == "overlay-kept"
    assert_world(state["world_xyz"])
    for plane, observed in state["geometry"].items():
        geometry(observed, plane, INITIAL_XYZ)
    assert_view(state["display"], expected)
    assert state["source_bytes"] == source_raw(edited_z)
    for plane, observed in state["plane_bytes"].items():
        assert observed == plane_raw(plane, INITIAL_XYZ, edited_z)


def python_api_consumer():
    ij = importlib.import_module("imagejxx")
    np = importlib.import_module("numpy")
    source = np.frombuffer(source_raw(), dtype="<f4").reshape(SHAPE_ZYX).copy()
    calibration = ij.SpatialCalibration(**CALIBRATION)
    document = ij.Document(ij.Dataset(source, axes="ZYX", calibration=calibration),
                           "P05d owned calibrated XYZ numeric zoom")
    application = ij.Application()
    identifier = application.adopt(document)
    try:
        prep = application.begin_edit(identifier)
        prep.set_property("zoom_property", "preserve")
        prep.set_extension("zoom_unknown", "keep")
        prep.set_rois([ij.ROI("retained-zoom-roi", ij.Selection("rectangle", [(2, 3), (10, 11)]),
                              z=3, attributes={"zoom_unknown": "roi-kept"})])
        prep.set_overlays([{"kind": "point", "points": [[7, 5]], "z": 3,
                            "zoom_unknown": "overlay-kept"}])
        application.commit(prep)
        view = application.snapshot(identifier).display
        for key, value in BASE_VIEW.items():
            setattr(view, key, value)
        view.properties = {"display_range": [0, 81631]}
        application.set_display(view, identifier)
        application.mark_saved(identifier)
        baseline = api_python_state(application, identifier)
        original = application.snapshot(identifier)
        assert_python_api_state(baseline, BASE_VIEW)
        previous = baseline
        expected = BASE_VIEW
        for percent, plane, linked in (("150", "XZ", False), ("60", "YZ", False), ("120", "YZ", True)):
            expected = expected_view(percent, plane, linked, expected)
            view = application.snapshot(identifier).display
            for key, value in expected.items():
                setattr(view, key, value)
            application.set_display(view, identifier)
            current = api_python_state(application, identifier)
            assert_python_api_state(current, expected)
            for key in previous:
                if key not in ("display", "revisions"):
                    assert current[key] == previous[key], key
            assert current["revisions"]["display"] > previous["revisions"]["display"]
            assert {k: v for k, v in current["revisions"].items() if k != "display"} == {
                k: v for k, v in previous["revisions"].items() if k != "display"}
            previous = current
        for invalid in (float("nan"), float("inf"), -float("inf"), -1.0, 1e9 + 1):
            for field in ("zoom", "plane_zoom"):
                before = api_python_state(application, identifier)
                view = application.snapshot(identifier).display
                if field == "zoom":
                    view.zoom = invalid
                else:
                    changed = list(view.plane_zoom)
                    changed[1] = invalid
                    view.plane_zoom = changed
                with pytest.raises(ValueError):
                    application.set_display(view, identifier)
                assert api_python_state(application, identifier) == before
        before_edit = api_python_state(application, identifier)
        transaction = application.begin_edit(identifier, ij.Change.PIXELS)
        replacement = np.frombuffer(plane_raw("XY", edited_z=3), dtype="<f4").reshape(17, 32).copy()
        transaction.replace_plane(replacement, z=3)
        application.commit(transaction)
        changed = api_python_state(application, identifier)
        assert_python_api_state(changed, expected, 3)
        assert changed["undo_steps"] == before_edit["undo_steps"] + 1 and changed["dirty"] is True
        application.undo(identifier)
        undone = api_python_state(application, identifier)
        assert_python_api_state(undone, expected)
        application.redo(identifier)
        redone = api_python_state(application, identifier)
        assert_python_api_state(redone, expected, 3)
        assert_history_delta(before_edit, changed, undone, redone)
        assert original.dataset.numpy("ZYX").tobytes() == source_raw()
        for state in (changed, undone, redone):
            for key in ("metadata", "overlays", "rois", "calibration"):
                assert state[key] == before_edit[key], key
        assert source.tobytes() == source_raw()
    finally:
        application.close(identifier, discard=True)
        assert application.document_ids == []



def control(rendered, selector):
    if selector.startswith("tools.") or selector.startswith("menu."):
        return rendered["host_controls"][selector]
    if selector.startswith("orthogonal_canvas."):
        plane = selector.split(".", 1)[1]
        return next(row for row in rendered["orthogonal_planes"] if row["plane"] == plane)
    if selector == "error_modal.controls.ok":
        return rendered["error_modal"]["controls"]["ok"]
    raise AssertionError("Unexpected physical selector: " + selector)


def click_sequence(observation):
    return [[{"type": "position", "space": "screen", "point": observation["position"]}],
            [{"type": "button", "name": "left", "down": True}],
            [{"type": "button", "name": "left", "down": False}], []]


def valid_control(value):
    integer(value["id"], True)
    for key in ("enabled", "active", "focused"):
        assert type(value[key]) is bool
    assert value["enabled"] is True
    rectangle, point = value["rect"], value["point"]
    assert len(rectangle) == 4 and len(point) == 2
    assert all(math.isfinite(x) for x in rectangle + point)
    assert rectangle[0] < rectangle[2] and rectangle[1] < rectangle[3]
    assert rectangle[0] <= point[0] < rectangle[2] and rectangle[1] <= point[1] < rectangle[3]


def continuous_frames(row, adopted):
    frames = row["frames"]
    assert frames and len(frames) <= 1200
    assert [frame["ordinal"] for frame in frames] == list(range(1, len(frames) + 1))
    epoch_sessions = {}
    keys, mouse = set(), [False, False, False]
    previous_epoch = None
    for frame in frames:
        epoch = frame["epoch"]
        assert epoch in {"primary", "fresh"}
        if previous_epoch is not None and epoch != previous_epoch:
            assert previous_epoch == "primary" and epoch == "fresh" and not keys and not any(mouse)
        rendered = frame["rendered"]
        session = rendered["session"]
        assert session["open"] is True
        identifier = integer(session["session_id"], True)
        context = integer(session["context_id"], True)
        assert context == session["current_context_id"]
        if epoch in epoch_sessions:
            prior = epoch_sessions[epoch]
            assert identifier == prior["session_id"] and context == prior["context_id"]
            assert session["imgui_frame"] == prior["imgui_frame"] + 1
            assert session["imgui_time"] > prior["imgui_time"]
        epoch_sessions[epoch] = session
        inputs = rendered["input_frame"]
        assert inputs["raw_inputs"] == frame["requested_events"]
        for event in frame["requested_events"]:
            if event["type"] in {"key", "keys"}:
                atoms = [event] if event["type"] == "key" else event["keys"]
                for atom in atoms:
                    assert type(atom["down"]) is bool
                    if atom["down"]:
                        keys.add(atom["name"])
                    else:
                        keys.discard(atom["name"])
            elif event["type"] == "button":
                assert event["name"] in {"left", "right", "middle"}
                mouse[{"left": 0, "right": 1, "middle": 2}[event["name"]]] = event["down"]
            elif event["type"] == "position":
                assert event["space"] == "screen"
                assert inputs["mouse_position"] == [math.floor(x) for x in event["point"]]
            elif event["type"] == "text":
                assert type(event["value"]) is str and event["value"]
            else:
                raise AssertionError("Unadopted input event: " + event["type"])
        assert set(inputs["keys_down"]) == keys
        assert inputs["mouse_down"] == mouse
        assert inputs["modifiers"] == {"ctrl": "Ctrl" in keys, "shift": "Shift" in keys,
                                      "alt": "Alt" in keys, "super": "Super" in keys}
        usage(frame["usage"])
        previous_epoch = epoch
    assert not keys and not any(mouse)
    if row["case_id"].endswith("session_and_pixel_undo"):
        assert set(epoch_sessions) == {"primary", "fresh"}
        assert epoch_sessions["primary"]["session_id"] != epoch_sessions["fresh"]["session_id"]
    else:
        assert set(epoch_sessions) == {"primary"}
    steps = row["steps"]
    assert len(steps) == len(adopted["steps"])
    assert [step["ordinal"] for step in steps] == list(range(1, len(steps) + 1))
    for step, expected in zip(steps, adopted["steps"]):
        assert step["adopted_step"] == expected and step["completed"] is True
        first, last = step["first_frame_ordinal"], step["last_frame_ordinal"]
        integer(first, True)
        integer(last)
        assert first <= last + 1 <= len(frames) + 1
        actual = [item for item in frames if item["step_ordinal"] == step["ordinal"]]
        if actual:
            assert first + 1 == actual[0]["ordinal"] and actual[-1]["ordinal"] == last
        if step.get("actual_condition_skipped") is True:
            assert expected["op"] == "click" and expected.get("if_actual_modal_open") is True
            assert not actual and first == last
            assert frames[last - 1]["rendered"]["error_modal"]["open"] is False
            continue
        if expected["op"] == "keys":
            assert any(item["requested_events"] == [{"type": "keys", "keys": expected["keys"]}]
                       for item in actual)
        elif expected["op"] == "text":
            text = [item for item in actual if item["requested_events"] == [
                {"type": "text", "value": expected["text"]}]]
            assert len(text) == 1
            observed = text[0]["rendered"]
            item = control(observed, "tools.zoom.percent")
            previous = frames[text[0]["ordinal"] - 2]["rendered"]
            assert previous["input_frame"]["focus"]["active_id"] == item["id"]
            assert previous["input_frame"]["input_text"]["id"] == item["id"]
            if expected["text"] != "1e999":
                assert observed["input_frame"]["focus"]["active_id"] == item["id"]
            assert observed["input_frame"]["input_text"]["id"] == item["id"]
            assert observed["input_frame"]["input_text"]["length"] == len(expected["text"])
        elif expected["op"] == "frame":
            assert actual and all(item["requested_events"] == [] for item in actual)
    for observation in row["control_observations"]:
        before = integer(observation["before_frame_ordinal"], True)
        step_ordinal = integer(observation["step_ordinal"], True)
        assert before < len(frames) and step_ordinal <= len(steps)
        current = control(frames[before - 1]["rendered"], observation["selector"])
        assert observation["control"] == current
        is_canvas = observation["selector"].startswith("orthogonal_canvas.")
        if is_canvas:
            integer(current["canvas_id"], True)
            assert current["visible"] is True
        else:
            valid_control(current)
        selected_id = integer(observation["selected_live_document_id"], True)
        if "document_id" in current:
            assert current["document_id"] == selected_id
        position = observation["position"]
        assert position == [math.floor(x) for x in position]
        if is_canvas:
            target = steps[step_ordinal - 1]["adopted_step"]["voxel_center"]
            for axis, voxel in enumerate(target):
                origin, scale = current["origin"][axis], current["scale"][axis]
                assert origin + voxel * scale <= position[axis] < origin + (voxel + 1) * scale
                center, extent = current["canvas_point"][axis], current["extent"][axis]
                assert center - extent / 2 <= position[axis] < center + extent / 2
        else:
            assert current["rect"][0] <= position[0] < current["rect"][2]
            assert current["rect"][1] <= position[1] < current["rect"][3]
        actual = frames[before:before + 4]
        assert [item["requested_events"] for item in actual] == click_sequence(observation)
        assert all(item["step_ordinal"] == step_ordinal for item in actual)
        selected = steps[step_ordinal - 1]["adopted_step"]
        moved = control(actual[0]["rendered"], observation["selector"])
        if is_canvas:
            assert selected["op"] == "click_actual_canvas"
            assert observation["selector"] == "orthogonal_canvas." + selected["plane"]
            assert moved["canvas_id"] == current["canvas_id"] and moved["visible"] is True
            assert control(actual[-1]["rendered"], observation["selector"])["focused"] is True
        else:
            assert selected["op"] == "click" and selected["selector"] == observation["selector"]
            assert moved["id"] == current["id"] and moved["enabled"] is True
            assert moved["rect"][0] <= position[0] < moved["rect"][2]
            assert moved["rect"][1] <= position[1] < moved["rect"][3]
    for step in steps:
        expected = step["adopted_step"]
        if expected["op"] in {"click", "click_actual_canvas"}:
            observations = [o for o in row["control_observations"] if o["step_ordinal"] == step["ordinal"]]
            # Conditional physical errorOK may already have been ACKed by real
            # Enter. Its skip must be explicitly observed, never inferred.
            assert len(observations) == (0 if step.get("actual_condition_skipped") is True else 1)
    return frames


def native_checkpoint(checkpoint, directory, expected, edited_z=None):
    state = checkpoint["native_state"]
    document = state["document"]
    xyz = expected["crosshair"]
    source_document(document, directory, xyz, edited_z, overlay_position="slice_index")
    native_camera(document, expected)
    assert state["active_document_id"] == document["document_id"]
    assert state["application_document_ids"] == [document["document_id"]]
    assert len(state["desktop_document_ids"]) == 1
    assert state["recording"] is False and state["workflow"] == [] and state["results"] == []
    rendered = checkpoint["rendered"]
    planes = rendered["orthogonal_planes"]
    assert [plane["plane"] for plane in planes] == ["XY", "XZ", "YZ"]
    for plane, coordinate, shape in zip(planes, (xyz[2], xyz[1], xyz[0]),
                                        ((17, 32, 1), (9, 32, 1), (9, 17, 1))):
        assert plane["visible"] is True
        assert plane["coordinate"] == coordinate
        assert plane["selection_spans"] == 0
        assert plane["pixels_source"] == "live_document"
        assert plane["source_document_id"] == document["document_id"]
        integer(plane["builds"], True)
        assert plane["preview_generation"] == 0
        readback = plane["readback"]
        assert readback["available"] is True
        image = readback["source"]
        assert image["shape"] == list(shape) and image["dtype"] == "float32"
        assert image["observation_scope"] == "actual CPU upload-input/cache, no GPU readback"
        assert raw_member(image["raw"], directory, shape) == plane_raw(plane["plane"], xyz, edited_z)
    assert_scales({item["plane"]: item["scale"] for item in planes}, expected)
    usage(checkpoint["usage"])
    assert checkpoint["usage"]["running"] == checkpoint["usage"]["queued"] == 0
    assert checkpoint["usage"]["reserved_bytes"] == 0
    return state


def checkpoint_map(row):
    values = row["checkpoints"]
    assert values and len({x["name"] for x in values}) == len(values)
    return {item["name"]: item for item in values}


def non_document_equal(before, after):
    assert {key: value for key, value in before.items() if key not in (
        "document", "active_document_id", "application_document_ids", "desktop_document_ids")} == {
        key: value for key, value in after.items() if key not in (
            "document", "active_document_id", "application_document_ids", "desktop_document_ids")}


def camera_cache_same(before, after):
    original, current = before["rendered"]["orthogonal_planes"], after["rendered"]["orthogonal_planes"]
    assert [item["builds"] for item in current] == [item["builds"] for item in original]


def control_axis(checkpoint, plane, expected):
    rendered = checkpoint["rendered"]
    percent = control(rendered, "tools.zoom.percent")
    assert percent["plane"] == plane and percent["plane_index"] == ("XY", "XZ", "YZ").index(plane)
    assert percent["horizontal_spacing"] == SPACING_XYZ[1 if plane == "YZ" else 0]
    assert percent["linked_zoom"] == expected["linked_zoom"]
    choice = control(rendered, "tools.zoom.plane")
    assert choice["selected_plane_after"] == plane
    assert choice["document_id"] == percent["document_id"]


def preparation_trace(row, adopted):
    inputs = row["api_inputs"]
    assert inputs and inputs[0]["step_ordinal"] == 0
    assert inputs[0]["purpose"] == "adopted labelled fixture/default preparation only"
    assert inputs[0]["methods"] == ["Window.addNativeDocument", "Window.setNativeDisplay", "Document.mark_saved"]
    integer(inputs[0]["document_id"], True)
    for item in inputs:
        assert item["tested_changes_use_API_setters"] is False
    if not row["case_id"].endswith("session_and_pixel_undo"):
        assert len(inputs) == 1
        return
    assert len(inputs) == 4
    for item, operation in zip(inputs[1:], ("native_action", "fresh_native_owner_open", "native_pixel_operation")):
        ordinal = integer(item["step_ordinal"], True)
        assert adopted["steps"][ordinal - 1]["op"] == operation
    saved, fresh, edited = inputs[1:]
    assert saved["method"] == "Window.action" and saved["name"] == "save_workspace"
    assert saved["purpose"] == adopted["steps"][saved["step_ordinal"] - 1]["purpose"]
    assert fresh["methods"] == ["Window(same_observed_preferences_path)", "Window.openFile"]
    assert fresh["purpose"] == "adopted fresh native Window/session open; no camera setter"
    assert saved["path"] == fresh["path"] == row["saved_session"]["path"]
    assert edited["methods"] == ["Application.resolve_target", "Application.prepare", "Application.publish"]
    assert edited["purpose"] == "adopted genuine native pixel edit; camera values are not changed byAPI"
    assert edited["operation"] == "add" and edited["parameters"] == {"value": 1}
    assert edited["document_id"] == fresh["document_id"]


def assert_ui_row(row, directory, adopted):
    assert row["schema_version"] == 1 and row["packet"] == "P05d"
    assert row["case_id"] == adopted["id"] and row["context"] in CONTEXTS
    assert row["contract_sha256"] == CONTRACT_SHA and row["adopted_group"] == adopted
    assert row["acceptance_claim"] is False
    # Status is metadata, never this independent test's source of truth.
    assert row["status"] == "passed"
    assert raw_member(row["fixture_input_raw"], directory, SHAPE_ZYX) == source_raw()
    preparation_trace(row, adopted)
    frames = continuous_frames(row, adopted)
    for frame in frames:
        assert decode(member_path(directory, f"frame-{frame['ordinal']}.json").read_bytes()) == frame
    checkpoints = checkpoint_map(row)
    for checkpoint in checkpoints.values():
        ordinal = integer(checkpoint["frame_ordinal"], True)
        assert checkpoint["rendered"] == frames[ordinal - 1]["rendered"]
        assert checkpoint["epoch"] == frames[ordinal - 1]["epoch"]
        assert decode(member_path(directory, f"checkpoint-{checkpoint['name']}.json").read_bytes()) == checkpoint
    baseline = checkpoints["baseline"]
    initial_view = dict(BASE_VIEW, linked_zoom=adopted["preparation"]["view"]["linked_zoom"])
    before = native_checkpoint(baseline, directory, initial_view)
    assert before["document"]["dirty"] is False
    original = before["document"]
    suffix = row["case_id"].removeprefix(PREFIX)
    expected = initial_view
    if suffix in {"independent_xz150", "independent_yz60", "linked_yz120"}:
        percent, plane, linked = {"independent_xz150": ("150", "XZ", False),
                                  "independent_yz60": ("60", "YZ", False),
                                  "linked_yz120": ("120", "YZ", True)}[suffix]
        expected = expected_view(percent, plane, linked)
        final = checkpoints["committed"]
        after = native_checkpoint(final, directory, expected)
        unchanged_native(original, after["document"], camera_change=True)
        non_document_equal(before, after)
        camera_cache_same(baseline, final)
        control_axis(final, plane, expected)
    elif suffix == "nonfinite_commit_atomic":
        for name in ("invalid_rejected", "after_ack"):
            state = native_checkpoint(checkpoints[name], directory, BASE_VIEW)
            unchanged_native(original, state["document"])
            non_document_equal(before, state)
            camera_cache_same(baseline, checkpoints[name])
        attempted = [frame for frame in frames if frame["requested_events"] == [
            {"type": "text", "value": "1e999"}]]
        assert len(attempted) == 1
        value = control(attempted[0]["rendered"], "tools.zoom.percent")
        assert value["value_changed"] is True and value["input_value_finite"] is False
        assert value["input_value_percent"] is None
        invalid_state = native_checkpoint(checkpoints["invalid_input_observed"], directory, BASE_VIEW)
        unchanged_native(original, invalid_state["document"])
        non_document_equal(before, invalid_state)
        failure = checkpoints["invalid_input_observed"]["rendered"]
        assert checkpoints["invalid_input_observed"]["step_ordinal"] == 6
        assert failure["window_error"] == "Zoom percentage must be finite"
        assert failure["error_modal"]["open"] is True
        assert checkpoints["after_ack"]["rendered"]["error_modal"]["open"] is False
    elif suffix == "numeric_focus_ownership":
        expected = expected_view("150", "XZ")
        focus_base = checkpoints["before_numeric_focus"]
        prior = native_checkpoint(focus_base, directory, expected)
        for name in ("numeric_owns_navigation", "normal_tab_exit"):
            state = native_checkpoint(checkpoints[name], directory, expected)
            unchanged_native(prior["document"], state["document"])
            non_document_equal(prior, state)
            camera_cache_same(focus_base, checkpoints[name])
        active = checkpoints["numeric_owns_navigation"]["rendered"]
        editor = control(active, "tools.zoom.percent")
        assert active["input_frame"]["focus"]["active_id"] == active["input_frame"]["input_text"]["id"] == editor["id"]
        assert active["orthogonal_navigation"]["keyboard_admitted"] is False
        exited = checkpoints["normal_tab_exit"]["rendered"]
        assert exited["input_frame"]["focus"]["active_id"] != editor["id"]
        expected = dict(expected, crosshair=[8, 5, 3])
        restored = checkpoints["canvas_navigation_restored"]
        after = native_checkpoint(restored, directory, expected)
        for key in ("document_id", "data_identity", "metadata", "rois", "overlays", "calibration",
                    "dirty", "history", "undo_steps", "redo_steps"):
            assert after["document"][key] == prior["document"][key], key
        assert {k: v for k, v in after["document"]["revisions"].items() if k != "display"} == {
            k: v for k, v in prior["document"]["revisions"].items() if k != "display"}
        last_key = [frame for frame in frames if frame["step_ordinal"] == 21 and
                    frame["requested_events"] == [{"type": "keys", "keys": [
                        {"name": "Shift", "down": True}, {"name": "Right", "down": True}]}]]
        assert len(last_key) == 1
        assert last_key[0]["rendered"]["input_frame"]["modifiers"]["shift"] is True
        assert last_key[0]["rendered"]["orthogonal_navigation"]["keyboard_admitted"] is True
        assert last_key[0]["rendered"]["orthogonal_navigation"]["focused_plane"] == 1
    else:
        assert suffix == "session_and_pixel_undo"
        expected = expected_view("60", "YZ", False, expected_view("150", "XZ"))
        saved = native_checkpoint(checkpoints["before_save"], directory, expected)
        reopened = native_checkpoint(checkpoints["reopened"], directory, expected)
        assert checkpoints["before_save"]["epoch"] == "primary" and checkpoints["reopened"]["epoch"] == "fresh"
        assert control(checkpoints["reopened"]["rendered"], "tools.zoom.plane")["selected_plane_after"] == "XY"
        non_document_equal(saved, reopened)
        for key in ("calibration", "metadata", "selection", "previous_selection", "rois", "overlays", "spectrum_present"):
            assert reopened["document"][key] == saved["document"][key], key
        changed = native_checkpoint(checkpoints["edited"], directory, expected, 3)
        undone = native_checkpoint(checkpoints["undone"], directory, expected)
        redone = native_checkpoint(checkpoints["redone"], directory, expected, 3)
        assert_history_delta(reopened["document"], changed["document"], undone["document"], redone["document"])
        assert changed["document"]["dirty"] is True and undone["document"]["dirty"] == reopened["document"]["dirty"]
        assert changed["document"]["data_identity"] != reopened["document"]["data_identity"]
        for state in (changed, undone, redone):
            for key in ("metadata", "selection", "previous_selection", "rois", "overlays", "calibration"):
                assert state["document"][key] == reopened["document"][key], key
            non_document_equal(reopened, state)
        for prior, current in ((checkpoints["reopened"], checkpoints["edited"]),
                               (checkpoints["edited"], checkpoints["undone"]),
                               (checkpoints["undone"], checkpoints["redone"])):
            assert all(new["builds"] > old["builds"] for old, new in zip(
                prior["rendered"]["orthogonal_planes"], current["rendered"]["orthogonal_planes"]))
    native_checkpoint(checkpoints["final"], directory, expected,
                      3 if suffix == "session_and_pixel_undo" else None)
    originals = row["original_after_raw"]
    expected_epochs = {"primary", "fresh"} if suffix == "session_and_pixel_undo" else {"primary"}
    assert set(originals) == expected_epochs
    for descriptor in originals.values():
        assert raw_member(descriptor, directory, SHAPE_ZYX) == source_raw()
    ui_cleanup(row, expected_epochs)
    return checkpoints



def ui_cleanup(row, epochs):
    cleanup = row["final_cleanup"]
    usage(cleanup["usage"])
    assert all(cleanup["usage"][key] == 0 for key in (
        "running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"))
    for key in ("application_document_ids", "desktop_document_ids",
                "primary_application_document_ids", "primary_desktop_document_ids"):
        assert cleanup[key] == []
    assert cleanup["creator_context_current"] is False
    assert cleanup["retained_observer_snapshots"] is False
    assert cleanup["retained_source_owner"] is False
    assert {entry["epoch"] for entry in cleanup["source_weak_owners"]} == epochs
    assert all(entry["expired"] is True for entry in cleanup["source_weak_owners"])
    lifecycle = row["lifecycle"]
    expected = [(epoch, label) for epoch in ("primary", "fresh") if epoch in epochs
                for label in ("opened", "before_close", "closed")]
    assert [(entry["epoch"], entry["label"]) for entry in lifecycle] == expected
    for epoch in epochs:
        opened, before, closed = [entry["observed"] for entry in lifecycle if entry["epoch"] == epoch]
        assert opened["open"] is True and before["open"] is True
        assert opened["session_id"] == before["session_id"] == closed["session_id"]
        assert opened["context_id"] == before["context_id"] == before["current_context_id"]
        assert closed["open"] is False and closed["context_id"] == closed["current_context_id"] == 0
        assert before["imgui_frame"] > opened["imgui_frame"]
        released = [frame for frame in row["frames"] if frame["epoch"] == epoch and
                    frame["label"] == "release_physical_inputs"]
        assert len(released) == 1
        actual = released[0]["rendered"]["input_frame"]
        assert actual["keys_down"] == [] and actual["mouse_down"] == [False, False, False]
        assert actual["modifiers"] == {"ctrl": False, "shift": False, "alt": False, "super": False}
        events = released[0]["requested_events"]
        assert len(events) == 4 and events[0]["type"] == "keys"
        assert {atom["name"] for atom in events[0]["keys"]} == {
            "Ctrl", "Shift", "Alt", "Super", "A", "Enter", "Tab", "Left", "Right", "Up", "Down",
            "PageUp", "PageDown", "Escape"}
        assert all(atom["down"] is False for atom in events[0]["keys"])
        assert events[1:] == [{"type": "button", "name": name, "down": False}
                              for name in ("left", "right", "middle")]
    if epochs == {"primary", "fresh"}:
        # Native pointer addresses may be reused after close; the real lifecycle
        # and distinct session IDs establish epochs without invented uniqueness.
        assert lifecycle[2]["label"] == "closed" and lifecycle[3]["label"] == "opened"
        assert lifecycle[2]["observed"]["session_id"] != lifecycle[3]["observed"]["session_id"]
    for job in cleanup["actual_native_jobs"]:
        assert job["state"].lower() in {"completed", "failed", "canceled"}


def read_cpp_ui(contract):
    configured = os.environ.get("IMAGEJXX_XYZ_NUMERIC_ZOOM_CPP_REPORT")
    assert configured, "Required fresh installed C++ numeric-zoom report missing"
    path = Path(configured).resolve(strict=True)
    report = decode(path.read_bytes())
    assert report["schema_version"] == 1 and report["packet"] == "P05d"
    assert report["context"] == "imgui_cpp" and report["acceptance_claim"] is False
    assert report["contract_sha256"] == CONTRACT_SHA
    assert [item["case_id"] for item in report["cases"]] == VARIANTS
    assert len(report["cases"]) == contract["group_count"] == 6
    return path.parent, report


@pytest.mark.parametrize(
    ("variant", "context"),
    [(variant, context) for variant in VARIANTS for context in CONTEXTS],
    ids=[variant + "-" + context for variant in VARIANTS for context in CONTEXTS])
def test_installed_xyz_numeric_zoom_ui(variant, context, xyz_contract, tmp_path):
    adopted = next(group for group in xyz_contract["groups"] if group["id"] == variant)
    if context == "imgui_cpp":
        root, report = read_cpp_ui(xyz_contract)
        number = VARIANTS.index(variant) + 1
        directory = (root / f"case{number:02d}").resolve(strict=True)
        assert directory.is_relative_to(root)
        row = report["cases"][number - 1]
        assert decode((directory / "case.json").read_bytes()) == row
    else:
        app = importlib.import_module("imagejxx.ui.app")
        preferences = importlib.import_module("imagejxx.ui.persistence")
        editor = app.Editor(preferences=preferences.Preferences(tmp_path / "preferences.json"))
        directory = tmp_path / "run"
        assert not directory.exists()
        row = decode(editor._editor._xyz_numeric_zoom_case(
            variant=variant, context=context, directory=str(directory)))
        assert decode((directory / "case.json").read_bytes()) == row
    assert row["context"] == context
    assert_ui_row(row, directory, adopted)
    if variant.endswith("session_and_pixel_undo"):
        saved = row["saved_session"]
        payload = member_path(directory, saved["path"]).read_bytes()
        assert len(payload) == saved["byte_count"]
        assert hashlib.sha256(payload).hexdigest() == saved["sha256"]


def sdk_consumer_report(root, report):
    assert report["schema_version"] == 1 and report["context"] == "cpp_api"
    assert report["case_id"] == PREFIX + "cpp_api" and report["contract_sha256"] == CONTRACT_SHA
    assert report["acceptance_claim"] is False and report["source_fixture_id"] == "plan_xyz_float32"
    assert report["raw_scope"] == "actual public Dataset.extract_plane; no renderer/cache/GPU"
    assert report["workflow_observation"] == {
        "available": False,
        "scope": "public Application has no recorder API; actual metadata.record_layout retained"}
    for field in ("GUI_observed", "cache_observed", "upstream_match_claim"):
        assert report[field] is False
    names = ["baseline", "independent_xz150", "independent_yz60", "linked_baseline", "linked_yz120",
             "changededit", "undo", "redo"]
    assert [item["name"] for item in report["checkpoints"]] == names
    states = {item["name"]: item["state"] for item in report["checkpoints"]}
    initial = states["baseline"]
    source_document(initial, root)
    native_camera(initial, BASE_VIEW)
    expected = expected_view("150", "XZ")
    source_document(states["independent_xz150"], root)
    native_camera(states["independent_xz150"], expected)
    unchanged_native(initial, states["independent_xz150"], True)
    expected = expected_view("60", "YZ", False, expected)
    source_document(states["independent_yz60"], root)
    native_camera(states["independent_yz60"], expected)
    unchanged_native(states["independent_xz150"], states["independent_yz60"], True)
    linked_base = dict(expected, linked_zoom=True)
    source_document(states["linked_baseline"], root)
    native_camera(states["linked_baseline"], linked_base)
    unchanged_native(states["independent_yz60"], states["linked_baseline"], True)
    expected = expected_view("120", "YZ", True, expected)
    source_document(states["linked_yz120"], root)
    native_camera(states["linked_yz120"], expected)
    unchanged_native(states["linked_baseline"], states["linked_yz120"], True)
    invalids = report["invalid_displays"]
    assert [item["name"] for item in invalids] == ["nan", "positive_infinity", "negative_infinity",
                                                 "negative", "above_maximum"]
    for item in invalids:
        encoded = item["requested_zoom_bits"]
        assert re.fullmatch(r"0x[0-9a-f]{16}", encoded)
        bits = int(encoded[2:], 16)
        if item["name"] == "nan":
            assert bits & 0x7FF0000000000000 == 0x7FF0000000000000 and bits & 0xFFFFFFFFFFFFF
        else:
            value = {"positive_infinity": float("inf"), "negative_infinity": -float("inf"),
                     "negative": -1.0, "above_maximum": 1e9 + 1}[item["name"]]
            assert bits == double_bits(value)
        assert item["exception_class"] == "invalid_argument" and "Display zoom" in item["error"]
        source_document(item["before"], root)
        source_document(item["after"], root)
        native_camera(item["before"], expected)
        native_camera(item["after"], expected)
        unchanged_native(item["before"], item["after"])
    for name, edited in (("changededit", 3), ("undo", None), ("redo", 3)):
        source_document(states[name], root, edited_z=edited)
        native_camera(states[name], expected)
        for field in ("metadata", "calibration", "selection", "previous_selection", "rois", "overlays"):
            assert states[name][field] == states["linked_yz120"][field]
    assert_history_delta(states["linked_yz120"], states["changededit"], states["undo"], states["redo"])
    assert states["changededit"]["data_identity"] != states["linked_yz120"]["data_identity"]
    assert states["changededit"]["dirty"] is True and states["undo"]["dirty"] == initial["dirty"]
    assert raw_member(report["original_after_raw"], root, SHAPE_ZYX) == source_raw()
    cleanup = report["final_cleanup"]
    assert cleanup["application_document_ids"] == [] and cleanup["active_document_id"] == 0
    usage(cleanup["usage"])
    assert all(cleanup["usage"][key] == 0 for key in (
        "running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"))
    assert cleanup["actual_native_jobs"] == []
    assert cleanup["source_snapshot_readable_after_close"] is True
    assert raw_member(cleanup["original_after_close_raw"], root, SHAPE_ZYX) == source_raw()


@pytest.mark.parametrize("context", ["cpp_api", "python_api"])
def test_installed_xyz_numeric_zoom_api(context, request):
    request.getfixturevalue("xyz_contract")
    if context == "python_api":
        python_api_consumer()
        return
    configured = os.environ.get("IMAGEJXX_XYZ_NUMERIC_ZOOM_CPP_API_REPORT")
    assert configured, "Required fresh installed public C++ SDK numeric-zoom report missing"
    path = Path(configured).resolve(strict=True)
    sdk_consumer_report(path.parent, decode(path.read_bytes()))
