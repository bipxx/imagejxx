"""Independent installed raw Auto observations in both shared ImGui launches.

The producer supplies observations only. Fixture membership, histogram bins,
cycle transitions, resolved actions and publication effects are test-owned.
Qualified prior readers supply transport/raw-input/processor mapping helpers.
"""
import copy
import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import struct
import sysconfig
import time

import pytest
import test_channel_display_ui as base
import test_channel_display_projection_ui as projection


CONTRACT = "p06c4.auto-contrast-ui.001"
CASES = tuple(f"U{i:02d}" for i in range(1, 9))
CONTEXTS = ("imgui_cpp", "imgui_python")
EXPECTED_COLLECTION_COUNT = 16
EXPECTED_COLLECTION_IDS = tuple(f"test_installed_auto_contrast[{case}-{context}]"
                                for case in CASES for context in CONTEXTS)
MAX_REPORT, MAX_ARCHIVE = 16*1024**2, 32*1024**2
VIEWPORT = (1250, 820)
installed_cpp_executable = projection.installed_cpp_executable
CHECKPOINTS = {
    "U01": ("cycle.first", "cycle.second", "cycle.third"),
    "U02": ("u16.auto", "u16.linked", "float.auto", "float.linked"),
    "U03": ("shift.all", "color.fallback", "gray.fallback"),
    "U04": ("roi.whole", "roi.area", "roi.nonarea"),
    "U05": ("busy.queued", "busy.disabled", "busy.jobs_open", "busy.canceled", "resource.refused"),
    "U06": tuple(f"stale.{kind}.{phase}" for kind in
                  ("pixels", "metadata", "selection", "display", "away_back", "closed") for phase in ("queued", "released")),
    "U07": ("workflow.original", "workflow.navigated", "workflow.replay", "workflow.rebind_opened",
             "workflow.rebind_values", "workflow.rebind", "workflow.malformed_refused"),
    "U08": ("preferences.default", "preferences.choice_before.12", "preferences.changed12", "preferences.frozen.queued",
             "preferences.choice_before.14", "preferences.frozen12", "preferences.changed14", "preferences.override8",
             "preferences.choice_before.auto", "preferences.restored_auto", "session.saved", "session.reopened"),
}
ACTION_LABELS = {
    "U01": ("cycle.first", "cycle.second", "cycle.third"),
    "U02": ("u16.auto", "float.auto"),
    "U03": ("shift.all", "color.fallback", "gray.fallback"),
    "U04": ("roi.whole", "roi.area", "roi.nonarea"),
    "U05": (), "U06": (),
    "U07": ("workflow.original", "workflow.replay", "workflow.rebind"),
    "U08": ("preferences.default", "preferences.changed12", "preferences.frozen12", "preferences.changed14",
             "preferences.override8", "preferences.restored_auto"),
}
FIXTURES = {
    "U01": {"source": ("uint8", (2, 2, 2, 64, 64, 1), "cycle", "composite")},
    "U02": {"source": ("uint16", (2, 3, 2, 6, 8, 1), "formula", "composite"),
             "float": ("float32", (2, 3, 2, 6, 8, 1), "float_formula", "composite")},
    "U03": {"source": ("uint16", (2, 3, 2, 4, 4, 1), "shift", "composite"),
             "color": ("uint16", (2, 9, 2, 3, 5, 1), "constant", "color"),
             "gray": ("uint16", (2, 9, 2, 3, 5, 1), "constant", "grayscale")},
    "U04": {"source": ("uint16", (2, 3, 2, 6, 8, 1), "formula", "composite")},
    "U05": {"source": ("uint8", (2, 3, 2, 6, 8, 1), "formula", "composite")},
    "U06": {"source": ("uint8", (2, 3, 2, 6, 8, 1), "formula", "composite"),
             "other": ("uint8", (2, 3, 2, 3, 5, 1), "constant", "composite")},
    "U07": {"source": ("uint16", (2, 3, 2, 6, 8, 1), "formula", "composite"),
             "target": ("uint16", (2, 3, 2, 3, 5, 1), "constant", "composite")},
    "U08": {"source": ("uint16", (2, 3, 2, 3, 5, 1), "constant", "composite")},
}


@pytest.fixture(scope="module")
def standalone_cases(installed_cpp_executable):
    configured = os.environ.get("IMAGEJXX_AUTO_CONTRAST_CPP_REPORT")
    assert configured, "Set IMAGEJXX_AUTO_CONTRAST_CPP_REPORT to the installed path-only U index"
    path = Path(configured).resolve(strict=True)
    index = base._read(path)
    assert index["contract_id"] == CONTRACT and index["context"] == "imgui_cpp"
    assert type(index["case_count"]) is int and index["case_count"] == 8
    assert index["observation_state"] == "observed" and index["acceptance_claim"] is False
    assert 0 <= base._number(index["elapsed_seconds"]) <= 60 and len(index["cases"]) == 8
    result = {}
    for entry in index["cases"]:
        assert set(entry) == {"case_id", "id", "relative_path", "byte_count", "sha256"}
        case = entry["case_id"]
        assert case == entry["id"] and case in CASES and case not in result
        member = base._member(path.parent, entry["relative_path"])
        raw = member.read_bytes()
        assert 0 < len(raw) <= MAX_REPORT and len(raw) == base._integer(entry["byte_count"], minimum=1)
        assert hashlib.sha256(raw).hexdigest() == entry["sha256"]
        result[case] = member
    assert tuple(result) == CASES and len(set(result.values())) == 8
    return result


def _observe(case, context, directory, request):
    if context == "imgui_cpp":
        path = request.getfixturevalue("standalone_cases")[case]
        return base._read(path), path.parent
    assert not directory.exists()
    started = time.monotonic()
    desktop = importlib.import_module("imagejxx._desktop")
    module = Path(desktop.__file__).resolve(strict=True)
    roots = {Path(sysconfig.get_path(key)).resolve()/"imagejxx" for key in ("purelib", "platlib")}
    assert module.suffix in {".pyd", ".so"} and any(module.is_relative_to(root) for root in roots)
    editor = desktop.Editor(preferences_path=str(directory.parent/"preferences.json"))
    row = base._decode(editor._auto_contrast_ui_case(case, context, str(directory)))
    assert row == base._read(directory/"case.json") and time.monotonic()-started <= 60
    return row, directory


def _source_plane(shape, dtype, position, recipe):
    t, c, z = position
    ysize, xsize, samples = shape[3:]
    assert samples == 1
    if recipe == "cycle":
        assert (ysize, xsize) == (64, 64) and dtype == "uint8"
        values = [10]+[20]*3+[30]*200+[40]*200+[50]*200+[60]*3492
    elif recipe == "constant":
        values = [7]*(ysize*xsize)
    elif recipe == "shift":
        assert (ysize, xsize) == (4, 4)
        values = [1000+257*(4*y+x) for y in range(ysize) for x in range(xsize)]
    else:
        assert recipe in {"formula", "float_formula"}
        values = [40*t+30*c+5*z+2*y+x for y in range(ysize) for x in range(xsize)]
        if recipe == "float_formula":
            values = [-2.5+.5*v for v in values]
    return values


def _fixture(source, shape, dtype, recipe):
    assert source["shape"] == list(shape) and source["dtype"] == dtype
    assert source["sample_policy"] == "finite_only" and source["calibration"] == base.CALIBRATION
    for position, plane in base._plane_rows(source).items():
        values = _source_plane(shape, dtype, position, recipe)
        assert bytes.fromhex(plane["storage_f64_hex"]) == b"".join(struct.pack("<d", float(v)) for v in values)
        assert bytes.fromhex(plane["declared_hex"]) == b"".join(struct.pack("<"+base.TYPE_CODES[dtype], v) for v in values)


def _selection_members(selection, width, height):
    """Independent cell membership for the explicitly frozen Auto operands."""
    if selection is None or selection["kind"] in {"point", "multipoint", "line", "polyline", "angle"}:
        return list(range(width*height))
    if selection["kind"] == "rectangle":
        assert len(selection["points"]) == 2
        a, b = selection["points"]
        return [y*width+x for y in range(height) for x in range(width)
                if min(a[0], b[0]) <= x < max(a[0], b[0]) and min(a[1], b[1]) <= y < max(a[1], b[1])]
    raise AssertionError("Unfrozen Auto fixture selection")


def _model(source, previous, *, scope, policy, bits):
    assert scope in {"active_channel", "composite_all"} and policy in {"active_selection", "whole_plane"}
    assert type(previous) is int and previous in {0, 5000, 2500, 1250, 625, 312, 156, 78, 39, 19, 9}
    assert type(bits) is int and bits in {0, 8, 10, 12, 14, 15, 16}
    shape, display, dtype = source["shape"], source["display"], source["dtype"]
    position = (display["t"], display["c"], display["crosshair"][2])
    raw = bytes.fromhex(base._plane_rows(source)[position]["storage_f64_hex"])
    plane = list(struct.unpack("<"+"d"*(len(raw)//8), raw))
    assert all(math.isfinite(v) for v in plane)
    selection = source["selection"] if policy == "active_selection" else None
    members = _selection_members(selection, shape[4], shape[3])
    area = [plane[i] for i in members]
    assert area
    minimum, maximum = min(area), max(area)
    if dtype == "uint8":
        origin, width = 0., 1.
        indices = [int(v) for v in area]
    elif dtype == "uint16":
        origin, width = minimum, (maximum-minimum+1)/256
        reciprocal = 1/width
        indices = [min(255, int((v-origin)*reciprocal)) for v in area]
    else:
        assert dtype == "float32"
        origin, width = minimum, (maximum-minimum)/256
        scale = 256/(maximum-minimum) if maximum != minimum else 0.
        indices = [min(255, int((v-origin)*scale)) for v in area]
    frequencies = [0]*256
    for index in indices:
        assert 0 <= index < 256
        frequencies[index] += 1
    used = 5000 if previous < 10 else previous//2
    eligible = [i for i, count in enumerate(frequencies) if len(area)//used < count <= len(area)//10]
    candidate = [origin+eligible[0]*width, origin+eligible[-1]*width] if eligible else None
    if candidate is None:
        outcome, following = "reset_fallback", 0
        resolved = [0., 255.] if dtype == "uint8" else [0., float((1 << bits)-1)] if dtype == "uint16" and bits else [min(plane), max(plane)]
    elif candidate[0] == candidate[1]:
        outcome, following, resolved = "area_extrema", used, [minimum, maximum]
    else:
        outcome, following, resolved = "histogram_bounds", used, candidate
    state = copy.deepcopy(base._stored(source))
    assert state is not None
    all_channels = scope == "composite_all" and state["mode"] == "composite"
    channels = (list(range(shape[1])) if all_channels or
                (outcome == "reset_fallback" and dtype == "uint16" and bits) else [position[1]])
    applied = ([float(struct.unpack("<f", struct.pack("<f", v))[0]) for v in resolved] if dtype == "float32"
               else [float(math.floor(v+.5)) for v in resolved]) if all_channels else list(resolved)
    for channel in channels:
        state["channels"][channel]["range"] = applied
    summary = {"domain":"raw", "binning":"processor_raw", "bins":256, "origin":origin, "bin_size":width,
               "minimum":minimum, "maximum":maximum, "visited_count":len(area), "finite_count":len(area),
               "nan_count":0, "plane_minimum":min(plane), "plane_maximum":max(plane),
               "plane_finite_count":len(plane), "plane_nan_count":0}
    return {"state":state, "channels":channels, "histogram":summary,
            "cycle":{"previous":previous, "used":used, "next":following},
            "decision":{"outcome":outcome, "indices":[eligible[0],eligible[-1]] if eligible else None,
                        "candidate_range":candidate, "resolved_range":resolved, "applied_range":applied},
            "selection":selection}


def _record(record, source, previous, *, scope, policy, bits):
    expected = _model(source, previous, scope=scope, policy=policy, bits=bits)
    assert set(record) == {"schema", "version", "effect", "roi", "display_dependency", "cycle", "scope",
                           "unsigned16_bits", "channels", "histogram", "decision"}
    assert record["schema"] == "imagejxx.auto-contrast-command" and type(record["version"]) is int and record["version"] == 1
    assert record["scope"] == scope and type(record["unsigned16_bits"]) is int and record["unsigned16_bits"] == bits
    for key in ("cycle", "histogram", "decision", "channels"):
        assert base._typed(record[key]) == base._typed(expected[key]), key
    selection = expected["selection"]
    effective = "whole_plane" if selection is None else "area" if selection["kind"] == "rectangle" else "whole_nonarea"
    assert base._typed(record["roi"]) == base._typed({"policy":policy, "effective":effective,
        "selection_revision":source["revisions"]["selection"], "selection":selection})
    assert base._typed(record["display_dependency"]) == base._typed({"revision":source["revisions"]["display"]})
    projection._record(record["effect"], source, expected["state"])
    return expected


def _publication(before, after, record, source, expected):
    assert after["document_ids"] == before["document_ids"] and after["facade_ids"] == before["facade_ids"]
    assert after["active_document_id"] == before["active_document_id"] == source["document_id"]
    assert after["results"] == before["results"] and after["recording"] is before["recording"]
    for original in before["documents"]:
        current = base._owner(after, original["document_id"])
        base._summary_unchanged(original, current, display=original["document_id"] != source["document_id"])
    current = base._owner(after, source["document_id"])
    projection._assert_stored(current, expected)
    assert current["revisions"]["display"] == source["revisions"]["display"]+1
    assert current["display"]["revision"] == source["display"]["revision"]+1
    for key in ("t", "c", "crosshair", "orthogonal", "linked_zoom", "zoom", "plane_zoom", "pan"):
        assert base._typed(current["display"][key]) == base._typed(source["display"][key])
    assert {k:v for k,v in current["display"]["properties"].items() if k != "channel_display"} == {
        k:v for k,v in source["display"]["properties"].items() if k != "channel_display"}
    assert before["recording"] is True
    assert after["workflow"] == [*before["workflow"], record]
    return current


def _control_aliases(row):
    """Give the qualified physical reader aliases from actual diagnostic fields."""
    frames = []
    for frame in row["frames"]:
        rendered = frame["rendered"]
        controls = dict(rendered["host_controls"])
        preferences = rendered["preferences_panel"]
        for name, control in preferences["widget_controls"].items():
            controls["preferences."+name] = control
        fields = preferences["fields"]
        if "conversion_unsigned16_range" in fields:
            field = fields["conversion_unsigned16_range"]
            assert field["name"] == "conversion_unsigned16_range" and field["kind"] == "choice"
            if field["combo"] is not None:
                controls["preferences.u16"] = field["combo"]
            for name, control in field["choices"].items():
                controls["preferences.u16.choice."+name] = control
        for job in rendered["jobs_panel"]["jobs"]:
            controls[f"jobs.cancel.{job['id']}"] = job["cancel"]
        frames.append(dict(frame, rendered=dict(rendered, host_controls=controls)))
    return dict(row, frames=frames)


def _physical(row):
    adapted = _control_aliases(row)
    assert row["drag_observations"] == []
    base._physical(adapted)
    owned = set()
    for observation in row["control_observations"]:
        n = observation["before_frame_ordinal"]
        owned.update(range(n+1, n+4))
        point = observation["position"]
        assert all(0 <= base._number(v) < bound for v, bound in zip(point, VIEWPORT))
        # Validate the pointer against the item actually rendered after moving,
        # not only the potentially stale rectangle preceding the position event.
        selector = observation["selector"]
        current = base._control(adapted["frames"][n]["rendered"], selector)
        assert current["id"] == observation["control"]["id"]
        if current["rect"] is not None:
            assert current["rect"][0] <= point[0] < current["rect"][2]
            assert current["rect"][1] <= point[1] < current["rect"][3]
        if selector == "channel_display.auto" and not observation["disabled_attempt"]:
            down = row["frames"][n+1]["rendered"]["input_frame"]
            assert down["focus"]["active_id"] == current["id"]
        if observation["disabled_attempt"]:
            before, after = row["frames"][n-1]["native_state"], row["frames"][n+2]["native_state"]
            _same_summary(before, after)
    for frame in row["frames"]:
        for event in frame["requested_events"]:
            assert event["type"] != "wheel", "No wheel input belongs to the frozen Auto timeline"
            if event["type"] in {"position", "button"}:
                assert frame["ordinal"] in owned, "Unattributed pointer input"


def _same_summary(before, after):
    for name in ("document_ids", "facade_ids", "active_document_id", "workflow", "results", "recording", "auto_divisor"):
        assert base._typed(after[name]) == base._typed(before[name]), name
    for source in before["documents"]:
        base._summary_unchanged(source, base._owner(after, source["document_id"]), display=True)


def _snapshot(row, label, owner_id=None):
    state, rendered = base._state(row, label)
    return state, rendered, base._owner(state, owner_id or row["source_document_id"])


def _initial_state(channels, mode):
    return {"schema":"imagejxx.channel-display", "version":1, "mode":mode,
            "channels":[{"visible":True, "range":[0.,255.],
                         "lut":{"name":("Red", "Green", "Blue")[c % 3]}} for c in range(channels)]}


def _fixtures(row, case):
    assert set(row["fixture_sources"]) == set(row["fixture_recipes"]) == set(FIXTURES[case])
    for label, (dtype, shape, recipe, mode) in FIXTURES[case].items():
        source = row["fixture_sources"][label]
        _fixture(source, shape, dtype, recipe)
        active = 8 if shape[1] == 9 else 1
        assert row["fixture_recipes"][label] == {"recipe":recipe, "shape":list(shape), "dtype":dtype,
                                                "position":[1,active,1], "mode":mode}
        assert [source["display"]["t"], source["display"]["c"], source["display"]["crosshair"]] == [1,active,[1,1,1]]
        projection._assert_stored(source, _initial_state(shape[1], mode))
        metadata = source["metadata"]
        assert metadata["title"] == f"Auto contrast {case} {label}"
        assert metadata["properties"] == {"source_only":{"nested":[True,7,"retained"]}, "revision_probe":7}
        assert metadata["extensions"] == {"future_annotation":{"version":37,"opaque":[3,1,4]}}
        assert metadata["record_layout"] == {"source_presence":True}
        rectangle = recipe in {"formula", "float_formula"} and case not in {"U05", "U06"}
        assert source["selection"] == ({"kind":"rectangle", "points":[[1.,1.],[7.,5.]], "width":1.} if rectangle else None)
        assert source["previous_selection"] == ({"kind":"rectangle", "points":[[1.,1.],[7.,5.]], "width":1.}
            if case in {"U05", "U06"} and label == "source" else
            {"kind":"line", "points":[[0.,0.],[2.,2.]], "width":1.})
        assert source["rois"] == [
            {"name":"explicit zero", "selection":{"kind":"rectangle", "points":[[1.,0.],[3.,2.]], "width":1.},
             "t":0, "c":0, "z":0, "attributes":{}},
            {"name":"wildcard", "selection":{"kind":"point", "points":[[2.,1.]], "width":1.},
             "t":None, "c":None, "z":1, "attributes":{}}]
        assert source["overlays"] == [] and source["spectrum_present"] is False
        assert source["dirty"] is False and source["undo_steps"] == 1 and source["redo_steps"] == 1


def _action(row, label):
    actions = [v for v in row["auto_observations"] if v["label"] == label]
    assert len(actions) == 1
    return actions[0]


def _check_action(row, label, previous, *, scope="active_channel", policy="active_selection", bits=0, replay=None):
    action = _action(row, label)
    fields = {"label", "trigger", "command_record", "source_before", "source_after", "state_before", "state_after", "confirm_frame_ordinal"}
    if label == "preferences.override8":
        fields.add("api_label")
        assert action["trigger"] == "api" and action["confirm_frame_ordinal"] is None
        assert action["api_label"] == "preferences.explicit_override8"
        assert base._api(row, action["api_label"])["accepted"] is True
    else:
        assert action["trigger"] == "physical"
        ordinal = base._integer(action["confirm_frame_ordinal"], minimum=1)
        controls = [v for v in row["control_observations"] if v["before_frame_ordinal"]+3 == ordinal]
        assert len(controls) == 1 and controls[0]["disabled_attempt"] is False
        selector = ("workflow.auto_contrast.0.replay" if label == "workflow.replay" else
                    "dialog.apply" if label == "workflow.rebind" else "channel_display.auto")
        assert controls[0]["selector"] == selector
        confirm = row["frames"][ordinal-1]
        assert confirm["requested_events"] == [{"type":"button", "name":"left", "down":False}]
        assert confirm["rendered"]["input_frame"]["modifiers"]["shift"] is (label == "shift.all")
    assert set(action) == fields
    source, after = action["source_before"], action["source_after"]
    record = action["command_record"]
    base._plane_rows(source)
    base._plane_rows(after)
    if replay is None:
        expected = _record(record, source, previous, scope=scope, policy=policy, bits=bits)
    else:
        assert record == replay["command_record"]
        expected = _record(record, replay["source_before"], previous, scope=scope, policy=policy, bits=bits)
    _publication(action["state_before"], action["state_after"], record, source, expected["state"])
    base._unchanged(source, after)
    projection._assert_stored(after, expected["state"])
    assert after["revisions"]["display"] == source["revisions"]["display"]+1
    state, rendered = base._state(row, label)
    assert base._typed(base._owner(state, after["document_id"])) == base._typed(after)
    assert state["workflow"] == action["state_after"]["workflow"]
    following = action["state_before"]["auto_divisor"] if replay is not None else expected["cycle"]["next"]
    if replay is None:
        assert action["state_before"]["auto_divisor"] == previous
    assert action["state_after"]["auto_divisor"] == state["auto_divisor"] == rendered["auto_contrast"]["divisor"] == following
    assert rendered["auto_contrast"]["busy"] is False and state["busy"] is False
    return action, expected


def _basics(row, case):
    if case == "U01":
        for label, previous, used, low in zip(ACTION_LABELS[case], (0,5000,2500), (5000,2500,1250), (10.,20.,30.)):
            action, expected = _check_action(row, label, previous)
            assert expected["decision"]["resolved_range"] == [low,50.]
            assert expected["cycle"]["next"] == used
            assert action["command_record"]["effect"]["position"] == {"t":1,"c":1,"z":1}
    elif case == "U02":
        for label, linked, bounds in (("u16.auto", "u16.linked", [78.,88.96875]),
                                      ("float.auto", "float.linked", [36.5,41.978515625])):
            action, expected = _check_action(row, label, 0)
            assert expected["decision"]["resolved_range"] == bounds
            ordinary = _snapshot(row, label, action["source_after"]["document_id"])
            orthogonal = _snapshot(row, linked, action["source_after"]["document_id"])
            for state, rendered, source in (ordinary, orthogonal):
                assert state["active_document_id"] == source["document_id"]
                base._unchanged(action["source_after"], source)
                x, y, z = source["display"]["crosshair"]
                images = rendered["orthogonal_planes"] if source["display"]["orthogonal"] else [rendered["ordinary_pixels"]]
                names = ("XY","XZ","YZ") if source["display"]["orthogonal"] else ("XY",)
                fixed = (z,y,x) if source["display"]["orthogonal"] else (z,)
                for image, plane, coordinate in zip(images, names, fixed):
                    _rgb(image, source, expected["state"], plane, coordinate, label.startswith("float"))
            assert ordinary[2]["display"]["orthogonal"] is False
            assert orthogonal[2]["display"]["orthogonal"] is True
    elif case == "U03":
        _, expected = _check_action(row, "shift.all", 0, scope="composite_all", bits=12)
        assert expected["decision"]["applied_range"] == [1000.,4841.]
        assert expected["channels"] == [0,1,2]
        for label, mode in (("color.fallback","color"), ("gray.fallback","grayscale")):
            _, expected = _check_action(row, label, 0, bits=12)
            assert expected["state"]["mode"] == mode and expected["channels"] == list(range(9))
            assert expected["decision"]["resolved_range"] == [0.,4095.] and expected["cycle"]["next"] == 0
    else:
        assert case == "U04"
        for label, previous, policy, bounds in (("roi.whole",0,"whole_plane",[75.,91.9453125]),
                ("roi.area",5000,"active_selection",[78.,88.96875]),
                ("roi.nonarea",2500,"active_selection",[75.,91.9453125])):
            action, expected = _check_action(row, label, previous, policy=policy)
            assert expected["decision"]["resolved_range"] == bounds
            assert action["command_record"]["roi"]["effective"] == {
                "roi.whole":"whole_plane", "roi.area":"area", "roi.nonarea":"whole_nonarea"}[label]
        change = base._api(row, "roi.nonarea.preparation")
        assert change["accepted"] is True and change["inputs"]["kind"] == "point"
        assert len([v for v in row["control_observations"] if v["selector"] == "channel_display.auto_roi"]) == 2


def _rgb(image, source, expected, plane, fixed, floating=False, *, constant=None):
    """Readback mapping using test-owned raw fixture values and prior byte rules."""
    shape, display, dtype = source["shape"], source["display"], source["dtype"]
    height = shape[3] if plane == "XY" else shape[2]
    width = shape[3] if plane == "YZ" else shape[4]
    colors, raw_samples = [], []
    channels = ([c for c,ch in enumerate(expected["channels"]) if ch["visible"]]
                if expected["mode"] == "composite" else [display["c"]])
    for v in range(height):
        for u in range(width):
            x, y, z = base._xyz(plane, fixed, u, v)
            components = []
            for c in channels:
                raw = 40*display["t"]+30*c+5*z+2*y+x
                value = constant if constant is not None else -2.5+.5*raw if floating else raw
                channel = expected["channels"][c]
                index = base._index(value, dtype, channel["range"])
                components.append([index]*3 if expected["mode"] == "grayscale" else base._lut(index, channel["lut"]))
            colors.append([min(255,sum(rgb[k] for rgb in components)) for k in range(3)])
            value = 40*display["t"]+30*display["c"]+5*z+2*y+x
            raw_samples.append(constant if constant is not None else -2.5+.5*value if floating else value)
    readback = image["readback"]
    assert readback["available"] is True and image["source_document_id"] == source["document_id"]
    assert image["current_tcz"] == image["range_tcz"] == readback["source_tcz"] == [display["t"],display["c"],display["crosshair"][2]]
    assert image["range"] == expected["channels"][display["c"]]["range"]
    actual = readback["rendered_rgb"]
    assert actual["shape"] == [height,width,3] and actual["dtype"] == "uint8"
    assert actual["pixels"] == [v for rgb in colors for v in rgb]
    assert all(type(v) in {int,float} and math.isfinite(v) and 0 <= v <= 255 and v == int(v) for v in actual["pixels"])
    diagnostic = image["channel_display"]
    assert base._typed(diagnostic["state"]) == base._typed(expected)
    assert [item["c"] for item in diagnostic["contributors"]] == channels
    for c,item in zip(channels,diagnostic["contributors"]):
        assert item["preview"] is False and item["generation"] == 0 and item["preview_z"] == []
        assert item["range"] == expected["channels"][c]["range"]
        base._integer(item["data_identity"],minimum=1)
    assert len({item["data_identity"] for item in diagnostic["contributors"]}) <= 1
    raw = readback["source"]
    assert raw["shape"] == [height,width,1] and raw["dtype"] == dtype
    assert b"".join(struct.pack("<d",v) for v in raw["pixels"]) == b"".join(struct.pack("<d",v) for v in raw_samples)


def _job(state, owner_id):
    jobs = [v for v in state["jobs"] if v["document_id"] == owner_id and v["label"] == "Auto contrast"]
    assert jobs
    for item in jobs:
        assert set(item) == {"id","document_id","label","state","state_code","error","resource_bytes",
            "cancel_requested","completed","total","checkpoints","stage","stage_completed","stage_total"}
        base._integer(item["id"],minimum=1)
        for key in ("state_code","resource_bytes","completed","total","checkpoints","stage_completed","stage_total"):
            base._integer(item[key])
        assert item["state"] in {"Queued","Running","Ready","Publishing","Completed","Failed","Canceled"}
        assert type(item["cancel_requested"]) is bool
    return max(jobs,key=lambda item:item["id"])


def _queued(row, label):
    state,rendered = base._state(row,label)
    source_id = row["source_document_id"]
    assert state["busy"] is rendered["auto_contrast"]["busy"] is True
    assert rendered["auto_contrast"]["owner_id"] == source_id
    assert state["recording"] is rendered["auto_contrast"]["recording_captured"] is True
    assert state["usage"]["running"] == state["usage"]["queued"] == 1
    # The one-byte running blocker is charged; queued work is reserved only
    # when the scheduler dispatches it (jobs.cpp's Running transition).
    assert state["usage"]["running_bytes"] == state["usage"]["reserved_bytes"] == 1
    assert state["usage"]["retained_bytes"] == 0
    job = _job(state,source_id)
    assert job["state"] == "Queued" and job["cancel_requested"] is False and job["resource_bytes"] > 1
    for selector in ("channel_display.auto","channel_display.reset_current"):
        assert rendered["channel_display_panel"]["control_states"][selector]["enabled"] is False
    return state,rendered,job


def _busy(row):
    initial,_ = base._state(row,"initial")
    queued,_,job = _queued(row,"busy.queued")
    assert row["queued_auto_job_id"] == job["id"]
    for label in ("busy.queued","busy.disabled","busy.jobs_open","busy.canceled","resource.refused"):
        state,_ = base._state(row,label)
        _same_summary(initial,state)
        base._unchanged(base._owner(initial,row["source_document_id"]),base._owner(state,row["source_document_id"]),display=True)
    attempts = [v for v in row["control_observations"] if v["disabled_attempt"]]
    assert [v["selector"] for v in attempts] == ["channel_display.auto","channel_display.reset_current"]
    opened,rendered = base._state(row,"busy.jobs_open")
    item = [v for v in rendered["jobs_panel"]["jobs"] if v["id"] == job["id"]]
    assert len(item) == 1 and item[0]["state"] == "Queued" and item[0]["cancel"]["enabled"] is True
    cancel = [v for v in row["control_observations"] if v["selector"] == f"jobs.cancel.{job['id']}"]
    assert len(cancel) == 1 and cancel[0]["disabled_attempt"] is False
    canceled,rendered = base._state(row,"busy.canceled")
    completed = _job(canceled,row["source_document_id"])
    assert completed["id"] == job["id"] and completed["state"] == "Canceled" and completed["cancel_requested"] is True
    assert canceled["busy"] is rendered["auto_contrast"]["busy"] is False and canceled["usage"] == base.ZERO_USAGE
    cap = base._api(row,"resource.cap_preparation")
    restore = base._api(row,"resource.restore_limits")
    assert cap["inputs"]["memory_budget"] == 1 and restore["inputs"]["memory_budget"] == 0
    refused,rendered = base._state(row,"resource.refused")
    assert refused["busy"] is False and refused["usage"] == base.ZERO_USAGE and refused["window_error"]
    assert rendered["error_modal"]["open"] is True
    assert len(refused["jobs"]) == len(canceled["jobs"]), "Cap refusal must precede a new job admission"


def _stale(row):
    source_id = row["source_document_id"]
    initial,_ = base._state(row,"initial")
    expected = copy.deepcopy(base._owner(initial,source_id))
    prior_job_id = 0
    for kind in ("pixels","metadata","selection","display","away_back","closed"):
        label = "stale."+kind
        queued,_,job = _queued(row,label+".queued")
        assert job["id"] > prior_job_id
        prior_job_id = job["id"]
        base._unchanged(expected,base._owner(queued,source_id),display=True)
        mutation = base._api(row,label+".mutation")
        assert mutation["accepted"] is True and mutation["inputs"]["kind"] == kind
        before,after = mutation["before"],mutation["after"]
        assert before["active_document_id"] == source_id
        released,rendered = base._state(row,label+".released")
        assert released["workflow"] == released["results"] == [] and released["auto_divisor"] == 0
        assert released["busy"] is rendered["auto_contrast"]["busy"] is False and released["usage"] == base.ZERO_USAGE
        latest = _job(released,source_id)
        assert latest["id"] == job["id"] and latest["state"] == ("Canceled" if kind == "closed" else "Failed")
        if kind != "closed":
            _same_summary(after,released)
            current = base._owner(released,source_id)
            if kind == "pixels":
                for position,plane in base._plane_rows(current).items():
                    values = ([76+2*y+x for y in range(6) for x in range(8)] if position == (1,1,1)
                              else _source_plane(current["shape"],"uint8",position,"formula"))
                    assert bytes.fromhex(plane["declared_hex"]) == bytes(values)
                    assert bytes.fromhex(plane["storage_f64_hex"]) == b"".join(struct.pack("<d",float(v)) for v in values)
                    if position != (1,1,1):
                        assert plane == base._plane_rows(expected)[position]
                for key in ("calibration","metadata","selection","previous_selection","rois","overlays","spectrum_present","display"):
                    assert base._typed(current[key]) == base._typed(expected[key])
                assert current["revisions"] == dict(expected["revisions"],pixels=expected["revisions"]["pixels"]+1)
                assert current["undo_steps"] == expected["undo_steps"]+1 and current["redo_steps"] == 0
                assert current["dirty"] is True and current["data_identity"] != expected["data_identity"]
            elif kind == "metadata":
                assert current["metadata"] == dict(expected["metadata"],properties=dict(expected["metadata"]["properties"],stale_probe=True))
                for key in ("planes","calibration","selection","previous_selection","rois","overlays","spectrum_present","display"):
                    assert base._typed(current[key]) == base._typed(expected[key])
                assert current["revisions"] == dict(expected["revisions"],metadata=expected["revisions"]["metadata"]+1)
                assert current["undo_steps"] == expected["undo_steps"]+1 and current["redo_steps"] == 0
                assert current["dirty"] is True and current["data_identity"] != expected["data_identity"]
            elif kind == "selection":
                assert current["selection"] == {"kind":"point","points":[[2.,2.]],"width":1.}
                assert current["previous_selection"] is None
                for key in ("planes","calibration","metadata","rois","overlays","spectrum_present","display","history","dirty","data_identity"):
                    assert base._typed(current[key]) == base._typed(expected[key])
                assert current["revisions"] == dict(expected["revisions"],selection=expected["revisions"]["selection"]+1)
            elif kind == "display":
                base._unchanged(expected,current)
                assert current["display"] == dict(expected["display"],zoom=2.5,revision=expected["display"]["revision"]+1)
                assert current["revisions"]["display"] == expected["revisions"]["display"]+1
            else:
                away = mutation["away_state"]
                assert away["active_document_id"] == row["fixture_sources"]["other"]["document_id"]
                assert after["active_document_id"] == source_id
                base._unchanged(expected,current,display=True)
                for state in (away,after):
                    assert state["workflow"] == state["results"] == [] and state["auto_divisor"] == 0
            expected = current
        else:
            base._unchanged(expected,mutation["closed_snapshot"],display=True)
            base._unchanged(expected,row["detached_sources"]["closed_source"],display=True)
            assert source_id not in after["document_ids"] and source_id not in released["document_ids"]
            assert after["active_document_id"] == released["active_document_id"] == row["fixture_sources"]["other"]["document_id"]
            _same_summary(after,released)
        other = row["fixture_sources"]["other"]
        base._unchanged(other,base._owner(released,other["document_id"]),display=True)


def _workflow(row):
    original,_ = _check_action(row,"workflow.original",0)
    assert row["original_record"] == original["command_record"] and row["original_owner_id"] == row["source_document_id"]
    navigated,_,source = _snapshot(row,"workflow.navigated")
    for key in ("planes","calibration","metadata","rois","overlays","history","dirty","data_identity"):
        assert base._typed(source[key]) == base._typed(original["source_after"][key])
    assert [source["display"]["t"],source["display"]["c"],source["display"]["crosshair"][2]] == [0,0,0]
    assert source["display"]["zoom"] == 1.25 and source["selection"] == {"kind":"point","points":[[2.,2.]],"width":1.}
    assert source["previous_selection"] == original["source_after"]["selection"]
    assert navigated["auto_divisor"] == 0
    replay,_ = _check_action(row,"workflow.replay",0,replay=original)
    assert replay["source_before"] == source
    assert replay["source_after"]["display"]["crosshair"] == source["display"]["crosshair"]
    opened,rendered = base._state(row,"workflow.rebind_opened")
    destination = row["fixture_sources"]["target"]["document_id"]
    assert row["rebind_owner_id"] == destination and opened["active_document_id"] == destination
    assert rendered["dialog"]["title"] == "Rebind Auto"
    prepared = base._api(row,"workflow.override_preference_preparation")
    assert prepared["accepted"] is True
    assert prepared["before"]["settings"]["conversion_unsigned16_range"] == "auto"
    assert prepared["after"]["settings"] == dict(prepared["before"]["settings"], conversion_unsigned16_range="12")
    _same_summary(prepared["before"],prepared["after"])
    assert opened["settings"]["conversion_unsigned16_range"] == "12"
    assert rendered["dialog"]["values"] == {"all_channels":False,"use_selection":True,"unsigned16_range":"12"}
    values,rendered = base._state(row,"workflow.rebind_values")
    assert rendered["dialog"]["values"] == {"all_channels":True,"use_selection":False,"unsigned16_range":"8"}
    _same_summary(opened,values)
    rebound,model = _check_action(row,"workflow.rebind",0,scope="composite_all",policy="whole_plane",bits=8)
    assert rebound["source_before"]["document_id"] == destination
    assert model["channels"] == [0,1,2] and model["decision"]["resolved_range"] == [0.,255.]
    assert rebound["state_before"]["settings"]["conversion_unsigned16_range"] == rebound["state_after"]["settings"]["conversion_unsigned16_range"] == "12"
    rejected = base._api(row,"workflow.malformed_original_refusal")
    assert rejected["accepted"] is False and rejected["error_type"] == "invalid_argument" and rejected["error"]
    malformed = copy.deepcopy(row["original_record"])
    malformed["decision"]["unexpected_late_field"] = True
    assert rejected["inputs"]["record"] == malformed
    _same_summary(rejected["before"],rejected["after"])
    refused,rendered = base._state(row,"workflow.malformed_refused")
    _same_summary(rejected["after"],refused)
    assert refused["window_error"] == "" and rendered["error_modal"]["open"] is False
    assert refused["workflow"] == [original["command_record"],original["command_record"],rebound["command_record"]]


def _preferences(row,directory):
    assert row["default_preferences"]["format"] == "imagejxx-preferences"
    assert row["default_preferences"]["values"]["conversion_unsigned16_range"] == "auto"
    for label,bits,policy in (("preferences.default",0,"active_selection"),
            ("preferences.changed12",12,"active_selection"),("preferences.frozen12",12,"active_selection"),
            ("preferences.changed14",14,"active_selection"),("preferences.override8",8,"whole_plane"),
            ("preferences.restored_auto",0,"active_selection")):
        action,model = _check_action(row,label,0,bits=bits,policy=policy)
        assert model["cycle"]["next"] == 0
        assert model["decision"]["resolved_range"] == ([7.,7.] if bits == 0 else [0.,float((1<<bits)-1)])
        assert model["channels"] == ([1] if bits == 0 else [0,1,2])
    for value,prior in (("12","auto"),("14","12"),("auto","14")):
        state,rendered = base._state(row,"preferences.choice_before."+value)
        field = rendered["preferences_panel"]["fields"]["conversion_unsigned16_range"]
        assert field["draft"] == prior and state["settings"]["conversion_unsigned16_range"] == prior
        choices = [v for v in row["control_observations"] if v["selector"] == "preferences.u16.choice."+value]
        assert len(choices) == 1 and choices[0]["disabled_attempt"] is False
        chosen = row["frames"][choices[0]["before_frame_ordinal"]+2]["rendered"]["preferences_panel"]
        assert chosen["fields"]["conversion_unsigned16_range"]["draft"] == value
    for value in ("12","14","auto"):
        applied = base._frame(row,"preferences.applied."+value)
        assert applied["native_state"]["settings"]["conversion_unsigned16_range"] == value
    base._text(row,"conversion_unsigned16_range")
    queued,_,job = _queued(row,"preferences.frozen.queued")
    frozen = _action(row,"preferences.frozen12")
    assert queued["workflow"] == frozen["state_before"]["workflow"] and queued["auto_divisor"] == 0
    for old in frozen["state_before"]["documents"]:
        base._summary_unchanged(old,base._owner(queued,old["document_id"]),display=True)
    assert frozen["state_before"]["settings"]["conversion_unsigned16_range"] == "12"
    assert frozen["state_after"]["settings"]["conversion_unsigned16_range"] == "14"
    assert _job(frozen["state_after"],row["source_document_id"])["id"] == job["id"]
    override = base._api(row,"preferences.explicit_override8")
    assert override["inputs"]["unsigned16_bits"] == 8
    assert override["before"]["settings"]["conversion_unsigned16_range"] == override["after"]["settings"]["conversion_unsigned16_range"] == "14"
    saved,_,source = _snapshot(row,"session.saved")
    assert row["original_owner_id"] == source["document_id"] == row["source_document_id"]
    assert row["original_records"] == saved["workflow"] and len(row["original_records"]) == 6
    assert all(record["effect"]["source"]["id"] == source["document_id"] for record in row["original_records"])
    session = base._session(row,directory,"native_session")
    assert len(session["documents"]) == 1 and session["workflow"] == row["original_records"] and session["results"] == saved["results"]
    base._session_document(session["documents"][0],source)
    reopened,_,fresh = _snapshot(row,"session.reopened",row["reopened_owner_id"])
    assert reopened["active_document_id"] == fresh["document_id"]
    projection._same_serialized_source(source,fresh)
    for name in ("t","c","crosshair","orthogonal","linked_zoom","zoom","plane_zoom","pan"):
        assert base._typed(fresh["display"][name]) == base._typed(source["display"][name])
    assert reopened["workflow"] == row["original_records"] and reopened["auto_divisor"] == 0
    assert fresh["document_id"] != row["original_owner_id"]
    base._text(row,str(base._member(directory,row["files"]["native_session"]["member"])))
    prefs = base._api(row,"preferences.fresh_controller_reopen")
    assert prefs["accepted"] is True
    assert prefs["preferences"]["format"] == "imagejxx-preferences"
    assert prefs["preferences"]["values"]["conversion_unsigned16_range"] == prefs["settings"]["conversion_unsigned16_range"] == "auto"
    _same_summary(prefs["before"],prefs["after"])
    _persisted_preferences(row,directory)


def _persisted_preferences(row,directory):
    raw = base._file(row,directory,"changed_preferences")
    captured = json.loads(raw.decode("utf-8"))
    assert captured["format"] == "imagejxx-preferences" and captured["version"] == 1
    assert captured["values"]["conversion_unsigned16_range"] == "12"
    applied = base._frame(row,"preferences.applied.12")
    assert captured["values"] == applied["native_state"]["settings"]
    source = row["changed_preferences_source"]
    assert set(source) == {"source_path","source_byte_count","source_hex","captured_member","saved_frame_ordinal"}
    assert Path(source["source_path"]).is_absolute()
    assert source["source_byte_count"] == len(raw) and source["source_hex"] == raw.hex()
    assert source["captured_member"] == row["files"]["changed_preferences"]["member"]
    retained = base._api(row,"preferences.capture_changed_file")
    assert retained["accepted"] is True
    for key,value in source.items():
        assert retained[key] == value
    assert source["saved_frame_ordinal"] == base._frame(row,"preferences.closed.12")["ordinal"]
    _same_summary(retained["before"],retained["after"])

    fresh = row["persisted_preferences"]
    assert fresh["schema_version"] == 1 and fresh["contract_id"] == CONTRACT
    assert fresh["case_id"] == "U08" and fresh["context"] == row["context"]
    assert fresh["stage"] == "persisted_changed_preferences" and fresh["budget_scope"] == "shared_parent_case"
    assert fresh["acceptance_claim"] is False and fresh["observation_state"] == "observed"
    assert not ({"pass","passed","expected","expected_rgb","expected_state"} & fresh.keys())
    assert fresh["limits"] == row["limits"]
    assert fresh["observation_storage"] == row["observation_storage"]
    assert fresh["parent_frame_offset"] == row["frame_count"]
    assert fresh["frame_count"] == 19
    assert row["total_frame_count"] == row["frame_count"] + fresh["frame_count"] <= 180
    assert 0 <= base._number(fresh["stage_elapsed_seconds"]) <= base._number(fresh["elapsed_seconds"]) <= row["elapsed_seconds"] <= 15
    assert fresh["preference_file"] == row["files"]["changed_preferences"]
    assert fresh["source_preparation"] == source and fresh["loaded_preferences"] == captured
    assert fresh["files"] == fresh["detached_sources"] == {}
    assert "persisted_preferences" not in fresh
    _fixtures(fresh,"U08")
    _physical(fresh)
    assert row["lifecycle"][-1]["open"] is False and fresh["lifecycle"][0]["open"] is True
    assert row["lifecycle"][-1]["session_id"] != fresh["lifecycle"][0]["session_id"]
    owner_id = fresh["source_document_id"]
    assert owner_id not in [item["document_id"] for item in row["fixture_sources"].values()]
    assert [item["selector"] for item in fresh["control_observations"]] == [
        f"document.{owner_id}","menu.Image","menu.action.show_channel_display","channel_display.auto"]
    assert [item["name"] for item in fresh["api_observations"]] == [
        "fixed_fixture_preparation","preferences.persisted12.bounded_wait"]
    wait = base._api(fresh,"preferences.persisted12.bounded_wait")
    assert wait["accepted"] is True and wait["inputs"] == {"method":"Window.action(wait_jobs)/Window.poll","timeout_ms":5000}
    assert [item["label"] for item in fresh["auto_observations"]] == ["preferences.persisted12"]
    checkpoints = ("persisted.initial","preferences.persisted12","persisted.final")
    assert set(fresh["states"]) == set(checkpoints)
    full = {fresh["states"][label]["frame_ordinal"] for label in checkpoints}
    assert len(full) == 3
    for frame in fresh["frames"]:
        for field in ("ordinary_pixels","orthogonal_planes"):
            assert (field in frame["rendered"]) is (frame["ordinal"] in full)
    initial,_ = base._state(fresh,"persisted.initial")
    fixture = fresh["fixture_sources"]["source"]
    base._unchanged(fixture,base._owner(initial,owner_id),display=True)
    assert initial["workflow"] == initial["results"] == [] and initial["auto_divisor"] == 0
    assert initial["settings"]["conversion_unsigned16_range"] == "12"
    action,model = _check_action(fresh,"preferences.persisted12",0,bits=12)
    assert model["decision"]["resolved_range"] == [0.,4095.] and model["channels"] == [0,1,2]
    assert action["state_before"]["settings"]["conversion_unsigned16_range"] == action["state_after"]["settings"]["conversion_unsigned16_range"] == "12"
    state,rendered,current = _snapshot(fresh,"preferences.persisted12")
    _rgb(rendered["ordinary_pixels"],current,model["state"],"XY",1,constant=7)
    final,_ = base._state(fresh,"persisted.final")
    base._unchanged(action["source_after"],base._owner(final,owner_id),display=True)
    assert final["workflow"] == [action["command_record"]] and final["results"] == []
    assert final["busy"] is False and final["window_error"] == "" and final["recording"] is True
    assert final["usage"] == fresh["final_usage"] == base.ZERO_USAGE


def _control_membership(row,case):
    source = row["source_document_id"]
    expected = [f"document.{source}","menu.Image","menu.action.show_channel_display"]
    if case == "U01":
        expected += ["channel_display.auto"]*3
    elif case == "U02":
        expected += ["channel_display.auto","orthogonal.toggle",
            f"document.{row['fixture_sources']['float']['document_id']}","channel_display.auto","orthogonal.toggle"]
    elif case == "U03":
        expected += ["channel_display.auto",f"document.{row['fixture_sources']['color']['document_id']}",
            "channel_display.auto",f"document.{row['fixture_sources']['gray']['document_id']}","channel_display.auto"]
    elif case == "U04":
        expected += ["channel_display.auto_roi","channel_display.auto","channel_display.auto_roi",
                     "channel_display.auto","channel_display.auto"]
    elif case == "U05":
        expected += ["channel_display.auto","channel_display.auto","channel_display.reset_current",
            f"jobs.cancel.{row['queued_auto_job_id']}","menu.Image","menu.action.show_channel_display",
            "channel_display.auto","error_modal.ok"]
    elif case == "U06":
        for kind in ("pixels","metadata","selection","display","away_back","closed"):
            expected.append("channel_display.auto")
            if kind != "closed":
                expected.append("error_modal.ok")
    elif case == "U07":
        expected += ["channel_display.auto","workflow.auto_contrast.0.replay",
            f"document.{row['fixture_sources']['target']['document_id']}","workflow.auto_contrast.0.rebind",
            "dialog.all_channels","dialog.use_selection","dialog.unsigned16_range",
            "dialog.unsigned16_range.choice.8","dialog.apply"]
    else:
        assert case == "U08"
        expected += ["channel_display.auto","menu.Edit","menu.Edit.Preferences","preferences.search"]
        for value in ("12","14","auto"):
            expected += ["preferences.u16","preferences.u16.choice."+value,"preferences.apply","preferences.close",
                         "channel_display.auto"]
            if value == "12":
                expected.append("channel_display.auto")  # Captured queued request at twelve bits.
        expected += ["menu.File","menu.path.save_document","browser.path","browser.submit",
                     "menu.File","menu.file.open","browser.path","browser.submit"]
    assert [v["selector"] for v in row["control_observations"]] == expected
    keys = [frame for frame in row["frames"] if any(event["type"] in {"key","keys"} for event in frame["requested_events"])]
    text = [frame for frame in row["frames"] if any(event["type"] == "text" for event in frame["requested_events"])]
    if case == "U03":
        assert [frame["label"] for frame in keys] == ["shift.all.shift.down","shift.all.shift.up"]
        assert [frame["requested_events"] for frame in keys] == [
            [{"type":"keys","keys":[{"name":"Shift","down":down}]}] for down in (True,False)]
    elif case == "U08":
        prefixes = ("preferences.search","session.save.path","session.open.path")
        assert [frame["label"] for frame in keys] == [prefix+".ctrl_a."+phase for prefix in prefixes for phase in ("down","up")]
        assert [frame["requested_events"] for frame in keys] == [
            [{"type":"keys","keys":[{"name":"Ctrl","down":down},{"name":"A","down":down}]}]
            for _ in prefixes for down in (True,False)]
        assert [frame["label"] for frame in text] == [prefix+".text" for prefix in prefixes]
        path = text[1]["requested_events"][0]["value"]
        assert text[0]["requested_events"] == [{"type":"text","value":"conversion_unsigned16_range"}]
        assert text[2]["requested_events"] == [{"type":"text","value":path}]
        assert Path(path).name == row["files"]["native_session"]["member"]
    else:
        assert keys == []
    if case != "U08":
        assert text == []


API_LABELS = {
    "U01": tuple(label+".bounded_wait" for label in ACTION_LABELS["U01"]),
    "U02": ("u16.auto.bounded_wait","float.auto.bounded_wait"),
    "U03": tuple(label+".bounded_wait" for label in ACTION_LABELS["U03"]),
    "U04": ("roi.whole.bounded_wait","roi.area.bounded_wait","roi.nonarea.preparation","roi.nonarea.bounded_wait"),
    "U05": ("busy.queue_preparation","busy.jobs_panel_preparation","busy.canceled.bounded_wait","busy.jobs_panel_close",
        "resource.cap_preparation","resource.restore_limits"),
    "U06": tuple(name for kind in ("pixels","metadata","selection","display","away_back","closed")
                 for name in (f"stale.{kind}.queue_preparation",f"stale.{kind}.mutation",f"stale.{kind}.released.bounded_wait")),
    "U07": ("workflow.original.bounded_wait","workflow.current_navigation_preparation","workflow.replay.bounded_wait",
        "workflow.override_preference_preparation","workflow.rebind.bounded_wait","workflow.malformed_original_refusal"),
    "U08": ("preferences.default.bounded_wait","preferences.capture_changed_file","preferences.changed12.bounded_wait","preferences.frozen.queue_preparation",
        "preferences.reopen.14","preferences.frozen12.bounded_wait","preferences.changed14.bounded_wait",
        "preferences.explicit_override8","preferences.override8.bounded_wait","preferences.reopen.auto",
        "preferences.restored_auto.bounded_wait","preferences.fresh_controller_reopen"),
}


def _api_ledger(row,case):
    assert [v["name"] for v in row["api_observations"]] == ["fixed_fixture_preparation",*API_LABELS[case]]
    fixed = base._api(row,"fixed_fixture_preparation")
    assert fixed["accepted"] is True and set(fixed) == {"name","role","methods","accepted"}
    for item in row["api_observations"][1:]:
        base._api(row,item["name"])
        ordinal = base._integer(item["before_frame_ordinal"],minimum=1)
        assert ordinal <= row["frame_count"]
        assert item["accepted"] is (item["name"] != "workflow.malformed_original_refusal")
        if item["name"].endswith(".bounded_wait"):
            assert item["inputs"] == {"method":"Window.action(wait_jobs)/Window.poll","timeout_ms":5000}
        if item["name"].endswith(".queue_preparation"):
            assert item["inputs"] == {"method":"JobScheduler.submit","workers":1}
            blocker = base._integer(item["blocker_job_id"],minimum=1)
            matches = [job for job in item["after"]["jobs"] if job["id"] == blocker]
            assert len(matches) == 1 and matches[0]["document_id"] == 0 and matches[0]["state"] == "Running"
            assert matches[0]["resource_bytes"] == 1


def _assert_case(row,case,context,directory):
    assert type(row["schema_version"]) is int and row["schema_version"] == 1
    assert row["contract_id"] == CONTRACT and row["case_id"] == case and row["context"] == context
    assert row["acceptance_claim"] is False and row["observation_state"] == "observed"
    assert not ({"pass","passed","expected","expected_rgb","expected_state"} & row.keys())
    assert row["observation_storage"] == "checkpoint_readbacks_full_intermediate_inputs_controls_native_summary"
    assert row["limits"] == {"max_frames":180,"max_seconds":15,"max_report_bytes":MAX_REPORT,
                             "max_archive_bytes":MAX_ARCHIVE,"max_documents":16}
    assert 0 <= base._number(row["elapsed_seconds"]) <= 15
    base._integer(row["source_document_id"],minimum=1)
    _physical(row)
    _control_membership(row,case)
    _fixtures(row,case)
    checkpoints = ("initial",*CHECKPOINTS[case],"final")
    assert set(row["states"]) == set(checkpoints)
    ordinals = [row["states"][name]["frame_ordinal"] for name in checkpoints]
    assert ordinals == sorted(set(ordinals))
    full_frames = set(ordinals)
    for frame in row["frames"]:
        for field in ("ordinary_pixels","orthogonal_planes"):
            assert (field in frame["rendered"]) is (frame["ordinal"] in full_frames)
    for name in checkpoints:
        state,rendered = base._state(row,name)
        assert rendered["workflow"] == state["workflow"]
        assert len(state["document_ids"]) <= 16
        for owner in state["documents"]:
            base._plane_rows(owner)
            assert owner["calibration"] == base.CALIBRATION
        summary = row["frames"][state["frame_ordinal"]-1]["native_state"]
        for owner in summary["documents"]:
            base._summary_unchanged(owner,base._owner(state,owner["document_id"]),display=True)
    initial,_ = base._state(row,"initial")
    assert initial["workflow"] == initial["results"] == [] and initial["recording"] is True
    assert initial["active_document_id"] == row["source_document_id"] and initial["auto_divisor"] == 0
    for fixture in row["fixture_sources"].values():
        base._unchanged(fixture,base._owner(initial,fixture["document_id"]),display=True)
    assert [v["label"] for v in row["auto_observations"]] == list(ACTION_LABELS[case])
    _api_ledger(row,case)
    assert set(row["files"]) == ({"native_session","changed_preferences"} if case == "U08" else set())
    assert sum(len(base._file(row,directory,name)) for name in row["files"]) <= MAX_ARCHIVE
    assert set(row["detached_sources"]) == ({"closed_source"} if case == "U06" else set())
    if case in {"U01","U02","U03","U04"}:
        _basics(row,case)
    elif case == "U05":
        _busy(row)
    elif case == "U06":
        _stale(row)
    elif case == "U07":
        _workflow(row)
    else:
        _preferences(row,directory)
    final,_ = base._state(row,"final")
    assert final["busy"] is False and final["window_error"] == "" and row["final_usage"] == base.ZERO_USAGE
    assert final["usage"] == base.ZERO_USAGE and final["recording"] is True
    assert final["results"] == []
    if case not in {"U05","U06"}:
        assert len(final["workflow"]) == len(ACTION_LABELS[case])


@pytest.mark.parametrize("context",CONTEXTS)
@pytest.mark.parametrize("case_id",CASES)
def test_installed_auto_contrast(case_id,context,request,tmp_path):
    row,directory = _observe(case_id,context,tmp_path/"observations",request)
    _assert_case(row,case_id,context,directory)
