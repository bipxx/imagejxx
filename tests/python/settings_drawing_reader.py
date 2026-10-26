"""Independent C3 report checks; no product imports, launches or rasterizer.

The caller authenticates original captures, source/product pins and owned
processes. This module checks their decoded observations against separate
literal expectations. A successful synthetic report is never runtime evidence.
"""
import hashlib
import json
import math
from pathlib import PurePosixPath, PureWindowsPath

from actual_observation_transport import decode_actual_observations
from settings_drawing_expectations import (
    ACCEPTED_SETTINGS, THIN_SETTINGS, WIDTH, HEIGHT, expected_mask, expected_rgb,
    initial_rgb, rounded_reference, storage_f64le_hex,
    ROUNDED_PATH_RELATIVE_TOLERANCE,
)

CASES = ("drawing-persist", "drawing-reopen")
CONTEXTS = ("imgui_cpp", "imgui_python")
DEFAULTS = {"foreground": "#ffffff", "background": "#000000", "brush_width": 8.,
            "rounded_corner_diameter": 8, "line_width": 1., "point_size": 8., "text_size": 16}
PHASES = ("before", "after", "undo", "redo", "restored")
PIXEL_OPERATIONS = ("fill", "clear", "brush", "eraser", "rounded", "line", "point")
THIN_OPERATIONS = ("thin_brush", "thin_line", "thin_point")
BOUNDS = {"frames": 750, "seconds": 60, "report_bytes_exclusive": 16 * 1024**2,
          "preference_bytes": 1024**2}


def _cycle_names(name):
    return tuple(name + "_" + phase for phase in PHASES)


COMMON = (*_cycle_names("fill"), *_cycle_names("clear"), *_cycle_names("brush"),
          *_cycle_names("eraser"), "rounded_constructed", "rounded_geometry_shown",
          *_cycle_names("rounded"), *_cycle_names("line"), *_cycle_names("point"),
          "text_opened", *_cycle_names("text"), "final")
STATES = {
    CASES[0]: ("initial", "preferences_shown", "thin_draft", "thin_applied", "thin_closed",
               *sum((_cycle_names(name) for name in THIN_OPERATIONS), ()),
               "final_draft", "final_applied", "final_closed", *COMMON),
    CASES[1]: ("initial", "preferences_shown", "reopened_closed", *COMMON),
}


def require(condition, message):
    if not condition:
        raise AssertionError("C3: " + message)


def _integer(value, minimum=0):
    return type(value) is int and value >= minimum


def _number(value):
    return type(value) in (int, float) and math.isfinite(value)


def _absolute(value):
    return isinstance(value, str) and bool(value) and (
        PurePosixPath(value).is_absolute() or PureWindowsPath(value).is_absolute())


def _inside(point, rect):
    require(type(point) in (list, tuple) and len(point) == 2 and
            type(rect) in (list, tuple) and len(rect) == 4 and
            all(_number(v) for v in (*point, *rect)), "finite input geometry")
    return rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]


def _near_vector(actual, expected):
    require(type(actual) in (list, tuple) and len(actual) == len(expected), "rounded vector shape")
    for a, b in zip(actual, expected):
        require(_number(a) and abs(a - b) <= ROUNDED_PATH_RELATIVE_TOLERANCE * (1 + abs(b)),
                "independent rounded reference coordinate")


def _pin(value):
    require(type(value) is dict and _absolute(value.get("path")) and
            _integer(value.get("bytes"), 1) and value["bytes"] <= 128 * 1024**2 and
            type(value.get("sha256")) is str and len(value["sha256"]) == 64 and
            all(c in "0123456789abcdef" for c in value["sha256"]), "runtime artifact pin")


def _json(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "duplicate preference key")
            result[key] = value
        return result
    def invalid(_):
        raise AssertionError("C3: nonfinite preference value")
    return json.loads(raw, object_pairs_hook=pairs, parse_constant=invalid)


def preference_payload(value):
    require(type(value) is dict and _absolute(value.get("path")) and
            type(value.get("exists")) is bool, "preference file identity")
    if not value["exists"]:
        require(value["byte_count"] == 0 and value["utf8"] is None and value["sha256"] is None,
                "absent preference file")
        return None
    require(type(value["utf8"]) is str, "original preference UTF8")
    raw = value["utf8"].encode("utf-8")
    require(0 < len(raw) == value["byte_count"] <= 1024**2 and
            hashlib.sha256(raw).hexdigest() == value["sha256"], "original preference bytes/hash")
    payload = _json(raw)
    require(type(payload) is dict and payload.get("format") == "imagejxx-preferences" and
            type(payload.get("version")) is int and payload["version"] == 1 and
            type(payload.get("values")) is dict and type(payload.get("extras")) is dict,
            "original preference grammar")
    return payload


def _opaque(payload, known):
    require(type(payload) is dict and type(payload.get("values")) is dict and
            type(payload.get("extras")) is dict, "preference object")
    extras = payload["extras"]
    # Window adds this known default when exposing preferences. The fixture
    # opens no files, so a populated/malformed list is still a failure.
    require("recent" not in extras or (type(extras["recent"]) is list and not extras["recent"]),
            "unexpected recent files in drawing-only workflow")
    return {"values": {k: v for k, v in payload["values"].items() if k not in known},
            "extras": {k: v for k, v in extras.items() if k != "recent"},
            "root": {k: v for k, v in payload.items() if k not in ("format", "version", "values", "extras")}}


def _field_control(frame, name):
    value = frame["report"]
    if name.startswith("field."):
        field = value["preferences_panel"]["fields"][name[6:]]
        return field["editor"] if field["kind"] == "color" else field
    if name.startswith("dialog."):
        return value["dialog"]["controls"][name[7:]]
    if name.startswith("geometry."):
        return value["selection_properties"]["control_states"][name[9:]]
    raise AssertionError("C3: unexpected text control " + name)


def _events(report, start, end, wanted):
    actual = [(e["kind"], e["control"], e.get("text")) for e in report["events"]
              if start <= e["frame_before"] and e["frame_after"] <= end]
    cursor = 0
    for item in wanted:
        while cursor < len(actual) and actual[cursor] != item:
            cursor += 1
        require(cursor < len(actual), "missing actual checkpoint input " + repr(item))
        cursor += 1


def _click(name):
    return "control_click", name, None


def _text(name, value):
    return "text_input", name, value


def _menu(name, selection=False):
    return [_click("host.menu.Edit"), *([_click("host.menu.Edit.Selection")] if selection else []), _click("host." + name)]


def _input(report):
    frames = report["frames"]
    require(type(frames) is list and 0 < len(frames) <= 750 and
            _number(report["elapsed_seconds"]) and 0 < report["elapsed_seconds"] < 60,
            "frame/time limits")
    for index, frame in enumerate(frames):
        observed = frame["report"]
        require(frame["ordinal"] == index and type(frame["events"]) is list and
                observed["input_frame"]["raw_inputs"] == frame["events"] and
                observed["window_error"] == "" and observed["input_frame"]["app_focus_lost"] is False,
                "actual frame/raw input/error identity")
    require(type(report["events"]) is list and report["events"], "actual input ledger")
    endings = [e["frame_after"] for e in report["events"]]
    require(all(_integer(n) for n in endings) and endings == sorted(set(endings)), "ordered actual input ledger")
    for event in report["events"]:
        start, end = event["frame_before"], event["frame_after"]
        require(_integer(start) and _integer(end) and start < end < len(frames), "input interval")
        # frame_before is the previous completed frame, not a second delivery
        # of its input (successive menu clicks can share this boundary).
        interval = frames[start + 1:end + 1]
        atoms = [a for frame in interval for a in frame["events"]]
        if event["kind"] == "control_click":
            item = event["actual_control"]
            require(_integer(item["id"], 1) and item.get("enabled", True) is True and
                    event["hovered_id"] == item["id"] and bool(event["hovered_window"]), "actual control ownership")
            require(_inside(event["actual_mouse"], item["rect"]) and
                    _inside(event["actual_mouse"], event["actual_clip"]), "click inside actual item/clip")
            if "window_name" in item:
                require(event["hovered_window"] == item["window_name"], "click owner window")
            require(any(f["report"]["actual_hover"] == {"id": item["id"], "window": event["hovered_window"]}
                        and f["report"]["input_frame"]["mouse_position"] == event["actual_mouse"] for f in interval),
                    "observed hovered input frame")
            buttons = [a["down"] for a in atoms if a.get("type") == "button" and a.get("name") == "left"]
            require(buttons == [True, False], "actual left press/release")
        elif event["kind"] == "text_input":
            require(_integer(event["input_id"], 1), "text input identity")
            delivered = [f for f in interval if {"type": "text", "value": event["text"]} in f["events"]]
            require(len(delivered) == 1, "exact delivered text")
            observed = delivered[0]["report"]["input_frame"]
            control = _field_control(delivered[0], event["control"])
            require(observed["focus"]["active_id"] == observed["input_text"]["id"] ==
                    control["id"] == event["input_id"] and control.get("enabled", True) is True and
                    control.get("disabled", False) is False and control.get("visible", True) is True,
                    "text actually consumed by intended field")
            enter = [a["down"] for a in atoms if a.get("type") == "key" and a.get("name") == "Enter"]
            require(enter == [True, False], "actual Enter pair")
            key_frames = [a["keys"] for a in atoms if a.get("type") == "keys"]
            require(len(key_frames) == 2 and key_frames[0] in (
                [{"name": "Ctrl", "down": True}, {"name": "A", "down": True}],
                [{"name": "Super", "down": True}, {"name": "A", "down": True}]) and
                key_frames[1] == [dict(a, down=False) for a in key_frames[0]], "actual select-all pair")
        else:
            require(event["kind"] == "canvas_position", "known actual event kind")
            canvas = event["canvas"]
            require(canvas["visible"] is True and _integer(canvas["canvas_id"], 1) and
                    event["point"] == event["actual_image_point"] and len(event["point"]) == 2,
                    "actual canvas/source coordinate")
            require(all(_number(v) for v in (*canvas["origin"], *canvas["scale"], *event["point"])) and
                    all(v > 1 for v in canvas["scale"]), "canvas transform")
            screen = [o + p*s for o, p, s in zip(canvas["origin"], event["point"], canvas["scale"])]
            require(screen == event["screen"] == frames[end]["report"]["input_frame"]["mouse_position"], "integer-centered physical input")
            if event["control"].endswith(".position"):
                require(event["hovered_id"] == canvas["canvas_id"], "gesture begins on actual canvas")
            current = frames[end]["report"]["ordinary_canvas"]
            require(all(current[k] == canvas[k] for k in ("canvas_id", "origin", "scale")), "stable actual canvas transform")


def _pixels(owner, values):
    require(owner["pixels"] == {"shape": [HEIGHT, WIDTH, 3], "hex": bytes(values).hex(),
            "storage_f64le_hex": storage_f64le_hex(values)}, "complete UInt8 and Float64 backing words")


def _selection_observation(observed, mask=None, curve=None):
    require(type(observed) is dict and observed["mask_shape"] == [HEIGHT, WIDTH], "selection observation shape")
    raw = observed["mask_u8_hex"]
    require(type(raw) is str and len(raw) == WIDTH * HEIGHT * 2 and
            all(raw[i:i+2] in ("00", "ff") for i in range(0, len(raw), 2)), "complete canonical mask")
    if mask is not None:
        require(raw == bytes(255 * bit for bit in mask).hex(), "independent complete selection mask")
    if curve is None:
        require(observed["rounded_path"] is None, "nonrounded geometry")
    else:
        _near_vector(observed["rounded_path"], curve)


def _plain_selection(kind, points, width):
    return {"kind": kind, "points": points, "width": width}


def _operation_selection(owner, name):
    if name in ("brush", "eraser", "thin_brush", "text"):
        require(owner["selection"] is None and owner["selection_observation"] is None, "unmasked brush/text route")
        return
    if name in ("fill", "clear"):
        selected = _plain_selection("rectangle", [[2, 2], [5, 5]], 3)
    elif name in ("line", "thin_line"):
        selected = _plain_selection("line", [[2, 4], [6, 4]], 1 if name.startswith("thin_") else 3)
    elif name in ("point", "thin_point"):
        selected = _plain_selection("point", [[4, 4]], 1 if name.startswith("thin_") else 5)
    else:
        require(name == "rounded", "known selection operation")
        reference = rounded_reference()
        selected = owner["selection"]
        require(set(selected) == {"kind", "points", "width", "corner_diameter"} and
                selected["kind"] == "rotated_rounded_rectangle" and selected["width"] == 3 and
                type(selected["corner_diameter"]) is int and selected["corner_diameter"] == 5,
                "real rotated rounded kind and captured diameter")
        x, y, ux, uy, vx, vy = reference["frame"]
        require(len(selected["points"]) == 3, "rounded native control count")
        for a, b in zip(selected["points"], ((x, y), (x+ux, y+uy), (x+vx, y+vy))):
            _near_vector(a, b)
        _selection_observation(owner["selection_observation"], reference["mask"], reference["curve"])
        return
    require(owner["selection"] == selected, "literal unstyled selection geometry/width")
    _selection_observation(owner["selection_observation"], expected_mask(name))


def _owner_shape(owner):
    require(_integer(owner["document_id"], 1) and _integer(owner["data_identity"]) and
            owner["scientific"] is False and owner["shape"] == [1, 1, 1, HEIGHT, WIDTH, 3] and
            owner["dtype"] == "uint8" and owner["sample_policy"] == "finite_only", "one ordinary RGB owner")
    require(type(owner["dirty"]) is bool and all(_integer(owner[k]) for k in ("undo", "redo")), "owner history types")
    require(set(owner["revisions"]) == {"pixels", "metadata", "selection", "display"} and
            all(_integer(v) for v in owner["revisions"].values()), "native revisions")
    history = owner["history"]
    require(history["undo_steps"] == owner["undo"] and history["redo_steps"] == owner["redo"] and
            _integer(history["step_limit"], 1) and _integer(history["pixel_budget"]) and
            history["pixels"]["exact"] is True and
            all(_integer(v) for k, v in history["pixels"].items() if k != "exact"), "history accounting observation")


def _selection_transitions(report, names, operation_names):
    states = report["states"]
    initial = states["initial"]["owners"][0]
    seed_mask = tuple(int(y == 28 and 30 <= x <= 38) for y in range(HEIGHT) for x in range(WIDTH))
    _selection_observation(initial["previous_observation"], seed_mask)
    # These transitions change selection only. The public Previous contract
    # captures the old active geometry, including null, at each publication.
    prior_selection = {
        "fill_before": "final_closed" if report["case_id"] == CASES[0] else "reopened_closed",
        "brush_before": "clear_restored", "rounded_constructed": "eraser_restored",
        "rounded_before": "rounded_constructed", "line_before": "rounded_restored",
        "point_before": "line_restored", "text_opened": "point_restored",
    }
    if report["case_id"] == CASES[0]:
        prior_selection.update(thin_line_before="thin_brush_restored", thin_point_before="thin_line_restored")
        require(states["thin_brush_before"]["owners"][0]["previous"] == initial["previous"] and
                states["thin_brush_before"]["owners"][0]["previous_observation"] == initial["previous_observation"],
                "unselected thin brush preserves initial Previous")
    for destination, source in prior_selection.items():
        owner, before = states[destination]["owners"][0], states[source]["owners"][0]
        require(owner["previous"] == before["selection"] and
                owner["previous_observation"] == before["selection_observation"], "selection publication captures Previous")
    edits = {(n + "_" + a, n + "_" + b) for n in operation_names for a, b in zip(PHASES, PHASES[1:])}
    for a, b in zip(names, names[1:]):
        if (a, b) in edits:
            continue
        before, after = states[a]["owners"][0], states[b]["owners"][0]
        for key in ("pixels", "metadata", "calibration", "rois", "overlays", "display", "data_identity",
                    "dirty", "undo", "redo", "history"):
            require(after[key] == before[key], "non-edit input preserves " + key)
        for key in ("pixels", "metadata", "display"):
            require(after["revisions"][key] == before["revisions"][key], "non-edit native revision")
        if b in prior_selection:
            require(after["revisions"]["selection"] > before["revisions"]["selection"], "actual selection publication revision")
        else:
            for key in ("selection", "previous", "selection_observation", "previous_observation"):
                require(after[key] == before[key], "non-selection input preserves " + key)
            require(after["revisions"]["selection"] == before["revisions"]["selection"], "non-selection revision preserved")


def _cycle(report, name):
    states = report["states"]
    owners = [states[name + "_" + phase]["owners"][0] for phase in PHASES]
    before, after, undone, redone, restored = owners
    for phase, owner in zip(PHASES, owners):
        _pixels(owner, expected_rgb(name) if phase in ("after", "redo") else initial_rgb())
        _operation_selection(owner, name)
        for key in ("selection", "previous", "selection_observation", "previous_observation", "display", "metadata", "calibration", "rois"):
            require(owner[key] == before[key], "edit cycle preserves " + key)
        expected_overlays = before["overlays"]
        if name == "text" and phase in ("after", "redo"):
            expected_overlays = expected_overlays + [{"kind": "text", "points": [[1, 1]], "width": 1,
                "text": "C3", "color": "#e01f40", "font_size": 23, "slice_index": 0}]
        require(owner["overlays"] == expected_overlays, "text-only overlay mutation")
    require(after["undo"] == redone["undo"] == before["undo"] + 1 and
            after["redo"] == redone["redo"] == 0 and
            undone["undo"] == restored["undo"] == before["undo"] and
            undone["redo"] == restored["redo"] == 1, "exact single edit Undo/Redo transactions")
    require(after["data_identity"] != before["data_identity"] and after["data_identity"] == redone["data_identity"] and
            before["data_identity"] == undone["data_identity"] == restored["data_identity"] and
            after["dirty"] is True and redone["dirty"] is True and
            undone["dirty"] == restored["dirty"] == before["dirty"], "native history identity restoration")
    changed = "metadata" if name == "text" else "pixels"
    for offset, owner in enumerate(owners):
        require(owner["revisions"][changed] == before["revisions"][changed] + offset,
                "one native revision per edit/history effect")
        for key in set(before["revisions"]) - {changed}:
            require(owner["revisions"][key] == before["revisions"][key], "unrelated edit revision")
    required = (_menu("menu.action.clear" if name == "clear" else "menu.action.fill")
                if name not in ("brush", "eraser", "thin_brush", "text") else
                [_click("dialog.apply")] if name == "text" else
                [_click("host.tools.tool." + ("eraser" if name == "eraser" else "brush")),
                 ("canvas_position", ("eraser" if name == "eraser" else "brush") + ".position", None)])
    _events(report, states[name+"_before"]["frame"], states[name+"_after"]["frame"], required)
    for first, last, command in (("after", "undo", "Undo"), ("undo", "redo", "Redo"), ("redo", "restored", "Undo")):
        _events(report, states[name+"_"+first]["frame"], states[name+"_"+last]["frame"], _menu("menu.Edit." + command))


def check_report(encoded, context=None, case_id=None):
    report = decode_actual_observations(encoded)
    require(report["schema"] == "c3.settings-drawing.ui.001" and report["state"] == "observed" and
            report["acceptance_claim"] is False and report["error"] == "" and report["bounds"] == BOUNDS,
            "successful bounded observation envelope")
    case = report["case_id"]
    require(case in CASES and report["context"] in CONTEXTS and (context is None or report["context"] == context)
            and (case_id is None or case == case_id), "exact route identity")
    runtime = report["runtime_before"]
    require(runtime == report["runtime_after"] and _integer(runtime["process_id"], 1) and
            _integer(runtime["parent_process_id"], 1) and runtime["pointer_bits"] == 64, "stable actual runtime")
    for key in ("executable", "producer_artifact", "native_artifact"):
        _pin(runtime[key])
    fixture = report["fixture"]
    require(fixture["runtime"] == {k: runtime[k] for k in ("process_id", "parent_process_id")} and
            fixture["producer_source"] == runtime["producer_source"] and fixture["imgui"] == runtime["imgui"] and
            fixture["shape"] == [1, 1, 1, HEIGHT, WIDTH, 3] and fixture["dtype"] == "uint8" and
            fixture["sample_formula"] == "RGB=[10+x,20+y,30+x+y]" and
            fixture["font_scope"] == "Text dialog/default and metadata only; no font or glyph parity claim", "declared fixture and scope")
    _input(report)
    states, names = report["states"], STATES[case]
    require(set(states) == set(names) and len(names) == (67 if case == CASES[0] else 47), "complete named checkpoint set")
    for i, name in enumerate(names):
        n = states[name]["frame"]
        require(_integer(n) and n < len(report["frames"]), "checkpoint frame")
        if i:
            prior = names[i-1]
            if (prior, name) in (("fill_restored", "clear_before"), ("text_restored", "final")):
                require(states[name] == states[prior], "intentional same-frame complete-state alias")
            else:
                require(n > states[prior]["frame"], "checkpoint progression")
    initial = states["initial"]
    known = set(initial["settings"])
    require(len(known) == 61 and set(ACCEPTED_SETTINGS) <= known, "complete 61-setting map")
    baseline = initial["owners"][0]
    opaque = _opaque(initial["preferences"], known)
    require(all(type(v) is dict and v for v in opaque.values()), "seeded nonempty unknown preference compartments")
    initial_file = preference_payload(initial["preferences_file"])
    require(initial_file is not None and _opaque(initial_file, known) == opaque, "actual caller-prepared preference seed")
    initial_values = DEFAULTS if case == CASES[0] else ACCEPTED_SETTINGS
    for key, value in initial_values.items():
        require(initial["settings"][key] == value and type(initial["settings"][key]) is not bool, "initial drawing settings")
    require(baseline["metadata"] == {"title": "C3 drawing", "properties": {"c3": {"label": "retain properties", "value": 17}},
            "extensions": {"c3": "retain extension"}, "record_layout": {"c3": "retain layout"}} and
            baseline["calibration"] == {"spacing": [1, 1, 1], "origin": [0, 0, 0], "direction": [1, 0, 0, 0, 1, 0, 0, 0, 1],
            "unit": "px", "frame_interval": 1, "time_origin": 0, "time_unit": "s"}, "literal source metadata/calibration")
    require(baseline["rois"] == [{"name": "C3 retained ROI", "selection": _plain_selection("rectangle", [[36, 28], [39, 31]], 1),
            "t": None, "c": None, "z": None, "attributes": {"c3": "retain ROI"}}] and
            baseline["overlays"] == [{"kind": "point", "points": [[39., 31.]], "width": 1, "c3": "retain overlay"}], "retained carrier fixtures")
    require(baseline["selection"] is None and baseline["selection_observation"] is None and
            baseline["previous"] == _plain_selection("line", [[30, 28], [38, 28]], 1), "initial selection/Previous")
    require(baseline["undo"] == baseline["redo"] == 0 and baseline["dirty"] is False, "fresh source history")
    operation_names = (*THIN_OPERATIONS, *PIXEL_OPERATIONS, "text") if case == CASES[0] else (*PIXEL_OPERATIONS, "text")
    edits = {name + "_" + phase: name for name in operation_names for phase in ("after", "redo")}
    for index, name in enumerate(names):
        state = states[name]
        require(len(state["owners"]) == 1 and state["active_id"] == baseline["document_id"] == state["owners"][0]["document_id"], "stable single owner")
        owner = state["owners"][0]; _owner_shape(owner)
        _pixels(owner, expected_rgb(edits[name]) if name in edits else initial_rgb())
        require(set(state["settings"]) == known and all(state["settings"][k] == initial["settings"][k]
                for k in known - set(ACCEPTED_SETTINGS)), "all 54 unrelated settings preserved")
        active = (initial_values if case == CASES[0] and index < names.index("thin_applied") else
                  THIN_SETTINGS if case == CASES[0] and index < names.index("final_applied") else ACCEPTED_SETTINGS)
        require(all(state["settings"][k] == v and type(state["settings"][k]) is not bool for k, v in active.items()), "applied settings phase")
        require(_opaque(state["preferences"], known) == opaque and
                all(state["preferences"]["values"][k] == state["settings"][k] for k in known), "native preferences and unknown compartments")
        payload = preference_payload(state["preferences_file"])
        require(payload is not None and _opaque(payload, known) == opaque, "saved unknown preference compartments")
        expected_file = (initial["preferences_file"] if case == CASES[1] or index < names.index("thin_applied") else
                         states["thin_applied"]["preferences_file"] if index < names.index("final_applied") else states["final_applied"]["preferences_file"])
        require(state["preferences_file"] == expected_file, "no unapproved preferences rewrite")
        if case == CASES[1] or index >= names.index("thin_applied"):
            require(all(payload["values"][k] == state["settings"][k] for k in known), "all applied settings persisted")
        require(state["error"] == "" and state["workflow"] == [] and state["results"] == [] and state["recording"] is False and
                state["usage"] == dict.fromkeys(("running", "queued", "running_bytes", "retained_bytes", "reserved_bytes"), 0), "settled native consumer state")
        for key in ("metadata", "calibration", "rois", "display"):
            require(owner[key] == baseline[key], "unchanged owner " + key)
        if name not in ("text_after", "text_redo"):
            require(owner["overlays"] == baseline["overlays"], "unrelated overlays unchanged")
        require(owner["history"]["step_limit"] == baseline["history"]["step_limit"] and
                owner["history"]["pixel_budget"] == baseline["history"]["pixel_budget"], "history policy unchanged")
    for name in operation_names:
        _cycle(report, name)
    _selection_transitions(report, names, operation_names)
    panel_phases = [("preferences_shown", initial_values)]
    if case == CASES[0]:
        panel_phases += [("thin_draft", THIN_SETTINGS), ("thin_applied", THIN_SETTINGS),
                         ("final_draft", ACCEPTED_SETTINGS), ("final_applied", ACCEPTED_SETTINGS)]
    for phase, target in panel_phases:
        panel = states[phase]["observations"]["preferences_panel"]
        require(panel["open"] is True and panel["error"] == "" and panel["group"] == "Drawing", "actual Drawing panel")
        for key, value in target.items():
            field = panel["fields"][key]
            require(field["draft"] == value and field["applied"] == states[phase]["settings"][key], "actual draft/applied field " + key)
    for name in (("thin_closed", "final_closed") if case == CASES[0] else ("reopened_closed",)):
        require(states[name]["observations"]["preferences_panel"]["open"] is False, "actual Preferences close")
    # Preferences are not image transactions, including the already populated redo queue.
    groups = (("initial", "preferences_shown", "thin_draft", "thin_applied", "thin_closed"),
              ("thin_point_restored", "final_draft", "final_applied", "final_closed")) if case == CASES[0] else (("initial", "preferences_shown", "reopened_closed"),)
    for group in groups:
        require(all(states[n]["owners"] == states[group[0]]["owners"] for n in group), "Preferences preserves complete owner/history")
    _recipe(report)
    return report


def _recipe(report):
    states = report["states"]
    def between(a, b, wanted):
        _events(report, states[a]["frame"], states[b]["frame"], wanted)
    opening = [_click("host.menu.Edit"), _click("host.menu.Edit.Preferences"), _click("preferences.group"), _click("group.Drawing")]
    between("initial", "preferences_shown", opening)
    if report["case_id"] == CASES[0]:
        order = (("foreground", "#e01f40"), ("background", "#1020e0"), ("brush_width", "1"),
                 ("rounded_corner_diameter", "5"), ("line_width", "1"), ("point_size", "1"), ("text_size", "23"))
        between("preferences_shown", "thin_draft", [_text("field."+k, v) for k, v in order])
        between("thin_draft", "thin_applied", [_click("preferences.apply")])
        between("thin_applied", "thin_closed", [_click("preferences.close_button")])
        between("thin_point_restored", "final_draft", opening + [_text("field."+k, v) for k, v in
                (("brush_width", "3"), ("line_width", "3"), ("point_size", "5"))])
        between("final_draft", "final_applied", [_click("preferences.apply")])
        between("final_applied", "final_closed", [_click("preferences.close_button")])
    else:
        between("preferences_shown", "reopened_closed", [_click("preferences.close_button")])
        require(not any(e["control"].startswith("field.") or e["control"] == "preferences.apply" for e in report["events"]), "fresh host consumes without restoring settings")
    texts = [e for e in report["events"] if e["kind"] == "text_input"]
    expected_texts = ([('field.' + k, v) for k, v in order] +
                      [("field.brush_width", "3"), ("field.line_width", "3"), ("field.point_size", "5")]
                      if report["case_id"] == CASES[0] else [])
    expected_texts += [("geometry." + k, v) for k, v in
                      (("Center X", "14.25"), ("Center Y", "12.75"), ("Width", "10.5"), ("Height", "7.5"), ("Angle (degrees)", "23"))]
    expected_texts.append(("dialog.text", "C3"))
    require([(e["control"], e["text"]) for e in texts] == expected_texts, "exact authored text recipe")
    gestures = [("final_closed" if report["case_id"] == CASES[0] else "reopened_closed", "fill_before", "rectangle"),
                ("eraser_restored", "rounded_constructed", "rounded_rectangle"),
                ("rounded_restored", "line_before", "line"), ("line_restored", "point_before", "point"),
                ("point_restored", "text_opened", "text")]
    if report["case_id"] == CASES[0]:
        gestures += [("thin_brush_restored", "thin_line_before", "line"), ("thin_line_restored", "thin_point_before", "point")]
    for start, end, tool in gestures:
        between(start, end, [_click("host.tools.tool."+tool), ("canvas_position", tool+".position", None)])
    literal_points = {"brush.position": [4, 4], "eraser.position": [4, 4], "rectangle.position": [2, 2], "rectangle.drag": [5, 5],
                      "rounded_rectangle.position": [9, 9], "rounded_rectangle.drag": [19.5, 16.5],
                      "line.position": [2, 4], "line.drag": [6, 4], "point.position": [4, 4], "text.position": [1, 1]}
    for event in report["events"]:
        if event["kind"] != "canvas_position":
            continue
        require(event["control"] in literal_points and event["point"] == literal_points[event["control"]], "frozen actual gesture point")
        if event["control"].endswith(".position"):
            first = event["frame_after"] + 1
            require(first < len(report["frames"]) and report["frames"][first]["events"] ==
                    [{"type": "button", "name": "left", "down": True}], "gesture actual press")
            release = first + (2 if event["control"].split(".")[0] in ("rectangle", "rounded_rectangle", "line") else 1)
            require(release < len(report["frames"]) and report["frames"][release]["events"] ==
                    [{"type": "button", "name": "left", "down": False}], "gesture actual release")
    constructed = states["rounded_constructed"]["owners"][0]
    require(constructed["selection"] == dict(_plain_selection("rounded_rectangle", [[9, 9], [20, 17]], 3), corner_diameter=5), "integer-snapped initial rounded gesture captures diameter5")
    require(states["rounded_geometry_shown"]["owners"] == states["rounded_constructed"]["owners"], "opening Properties is a draft")
    between("rounded_constructed", "rounded_geometry_shown", _menu("menu.action.show_selection_properties", True))
    between("rounded_geometry_shown", "rounded_before", [_text("geometry."+k, v) for k, v in
            (("Center X", "14.25"), ("Center Y", "12.75"), ("Width", "10.5"), ("Height", "7.5"), ("Angle (degrees)", "23"))] + [_click("geometry.Apply")])
    require(not any(e["control"] == "geometry.Corner diameter" for e in report["events"]), "geometry cannot substitute a new diameter")
    geometry = states["rounded_geometry_shown"]["observations"]["selection_properties"]
    require(geometry["open"] is True and geometry["corner_diameter"] == 5 and
            states["rounded_before"]["observations"]["selection_properties"]["open"] is False, "real rounded Properties lifecycle")
    text = states["text_opened"]["observations"]["dialog"]
    require(text["open"] is True and text["title"] == "Text annotation" and
            text["values"]["font_size"] == 23 and text["values"]["color"] == "#e01f40" and
            text["values"]["x"] == text["values"]["y"] == 1, "actual Text dialog defaults")
    between("text_opened", "text_before", [_text("dialog.text", "C3")])
    # InputText edits the live text buffer. Generic dialog values are defaults;
    # Apply parses that buffer into a separate local parameter object.
    require(states["text_before"]["observations"]["dialog"]["text"]["text"] == "C3" and
            states["text_after"]["observations"]["dialog"]["open"] is False, "actual Text draft/Apply lifecycle")
    setup = report["setup_events"]
    require([e["kind"] for e in setup] == ["fixture.source", "fixture.preferences_input", "session.closed"] and
            setup[0]["settings_mutation"] is False and setup[-1]["lifecycle"]["open"] is False and
            setup[-1]["lifecycle"]["context_id"] == 0 and setup[-1]["lifecycle"]["current_context_id"] == 0,
            "labelled fixture and closed ImGui session")
    require(setup[1]["preferences_file"] == states["initial"]["preferences_file"] and
            setup[1]["settings"] == states["initial"]["settings"] and
            setup[1]["preferences"] == states["initial"]["preferences"], "actual constructor preference input")
    prepared = setup[0]["owner"]
    initial = states["initial"]["owners"][0]
    for key in ("document_id", "data_identity", "shape", "dtype", "sample_policy", "pixels", "metadata", "calibration",
                "selection", "previous", "selection_observation", "previous_observation", "rois", "overlays", "dirty", "undo", "redo", "history"):
        require(prepared[key] == initial[key], "fixture-to-initial source preservation")


def check_pair(persist, reopen):
    persist = check_report(persist, case_id=CASES[0])
    reopen = check_report(reopen, persist["context"], CASES[1])
    require(persist["runtime_before"]["process_id"] != reopen["runtime_before"]["process_id"], "distinct fresh host processes")
    first, second = persist["states"]["final"], reopen["states"]["initial"]
    require(first["preferences_file"] == second["preferences_file"] and
            first["preferences"] == second["preferences"] and first["settings"] == second["settings"],
            "exact persisted bytes/path/settings/unknown compartments consumed by fresh host")
