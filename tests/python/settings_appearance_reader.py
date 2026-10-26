"""Independent C2 actual-input/GL reader. No product imports or product launch."""
import hashlib
import json
import math
import struct

from actual_observation_transport import decode_actual_observations
from settings_appearance_expectations import (
    ACCEPTED_SETTINGS, BACKGROUND_RGB, CHECKER_WORDS, STEP_WORDS,
    checker_plane, expected_gl_sample, framebuffer_uv,
)

CASES = ("appearance-persist", "appearance-reopen")
CONTEXTS = ("imgui_cpp", "imgui_python")
DEFAULTS = {"theme": "dark", "ui_scale": 1., "auto_dpi": True,
            "canvas_background": "#181b21", "default_zoom": 0., "interpolation": "nearest"}
STATES = {
    CASES[0]: ("before", "draft_cancelled", "draft_reverted", "invalid_scale", "invalid_color",
               "nearest_applied", "dpi_enabled_nearest", "new_owner", "ordinary_nearest",
               "linked_nearest", "linear_applied", "ordinary_sampled", "linked_sampled", "final_closed"),
    CASES[1]: ("before", "shown", "new_owner", "ordinary_sampled", "linked_sampled",
               "defaults_draft", "reverted", "final_closed"),
}


def require(condition, message):
    if not condition:
        raise AssertionError("C2: " + message)


def close(actual, expected, tolerance=1e-5):
    require(type(actual) in (int, float) and math.isfinite(actual)
            and abs(actual - expected) <= tolerance, f"{actual!r} != {expected!r}")


def vector(actual, expected, tolerance=1e-5):
    require(type(actual) in (list, tuple) and len(actual) == len(expected), "vector dimensions")
    for a, b in zip(actual, expected):
        close(a, b, tolerance)


def inside(point, rect, margin=0):
    require(len(point) == 2 and len(rect) == 4 and all(
        type(v) in (int, float) and math.isfinite(v) for v in (*point, *rect)), "finite geometry")
    require(rect[2] > rect[0] and rect[3] > rect[1], "positive rectangle")
    return rect[0] + margin <= point[0] < rect[2] - margin and rect[1] + margin <= point[1] < rect[3] - margin


def preference_bytes(row):
    require(type(row["exists"]) is bool, "file existence type")
    if not row["exists"]:
        require(row["byte_count"] == 0 and row["utf8"] is None and row["sha256"] is None, "absent file witness")
        return None
    raw = row["utf8"].encode("utf8")
    require(0 < len(raw) == row["byte_count"] <= 1048576, "preferences byte count")
    require(hashlib.sha256(raw).hexdigest() == row["sha256"], "preferences hash")
    require(type(json.loads(raw)) is dict, "preferences object")
    return raw


def check_gl_sample(sample, mode, *, fixed_index=0):
    """Compare glReadPixels with independent literal texture equations."""
    name, geometry, runtime, sampler = (sample[k] for k in ("surface", "geometry", "runtime", "sampler"))
    require(name in ("ordinary", "XY", "XZ", "YZ"), "unknown surface")
    linked = name != "ordinary"
    width, height = (2, 2) if linked else (2, 4)
    words = checker_plane(fixed_index) if linked else STEP_WORDS
    require(geometry["visible"] is True and not geometry.get("loading", False), "sampled visible completed canvas")
    require((geometry["width"], geometry["height"]) == (width, height), "fixture texture geometry")
    require(sampler["available"] is True and type(sampler["texture_id"]) is int and sampler["texture_id"] > 0,
            "actual created texture")
    require((sampler["width"], sampler["height"]) == (width, height), "actual GL texture dimensions")
    filtering = 9728 if mode == "nearest" else 9729
    require((sampler["min_filter"], sampler["mag_filter"]) == (filtering, filtering), "actual MIN/MAG filter")
    require((sampler["wrap_s"], sampler["wrap_t"]) == (10497, 10497), "unchanged actual GL_REPEAT")
    require(geometry["sampler"] == sampler, "surface and readback sampler disagree")
    rect, canvas = geometry["image_rect"], geometry["canvas_rect"]
    vector((rect[2] - rect[0], rect[3] - rect[1]),
           (width * geometry["scale"][0], height * geometry["scale"][1]))
    require(all(16 <= x <= 64 for x in geometry["scale"]), "magnified fixture")
    fw, fh = runtime["framebuffer_size"]
    require(all(type(v) is int and v > 0 for v in (fw, fh)), "actual framebuffer dimensions")
    require(runtime["viewport"] == [0, 0, fw, fh], "full framebuffer viewport")
    sx, sy = runtime["framebuffer_scale"]
    require(all(type(v) in (int, float) and math.isfinite(v) and v > 0 for v in (sx, sy)), "framebuffer scale")
    dx, dy = runtime["display_pos"]
    for key in ("vendor", "renderer", "version", "shading_language_version"):
        require(type(runtime[key]) is str and runtime[key].strip(), "actual GL identity " + key)
    crosshair = sample["crosshair_lines_logical"]
    require((crosshair is not None) == linked, "crosshair ownership")
    points = sample["points"]
    require(len(points) == 7, "seven independent sample points")
    bases = [(u, v) for v in (.375, .625) for u in (.375, .625)] + [(.25, .25), (.75, .75)]
    distinguishing = 0
    for ordinal, point in enumerate(points):
        image = ordinal < 6
        require(point["role"] == ("image" if image else "background"), "sample role")
        kind = "interior" if ordinal < 4 else "endpoint_near" if image else "background"
        require(point["sample_kind"] == kind, "sample kind")
        offset = point["avoidance_offset_logical"]
        expected_offset = [0., 0.]
        if image:
            uv = bases[ordinal]
            vector(point["base_uv"], uv)
            requested = [rect[axis] + uv[axis] * (rect[axis+2] - rect[axis]) for axis in (0, 1)]
            if linked and ordinal >= 4:
                for axis in (0, 1):
                    if abs(requested[axis] - crosshair[axis]) < 3:
                        expected_offset[axis] = 4. if requested[axis] < (rect[axis]+rect[axis+2])*.5 else -4.
            vector(offset, expected_offset)
            requested = [v + o for v, o in zip(requested, offset)]
        else:
            require(point["base_uv"] is None, "background has no texture UV")
            vector(offset, [0, 0])
            requested = [canvas[0] + 12, canvas[1] + 12]
        vector(point["requested_logical"], requested)
        require(inside(requested, canvas, 2), "sample inside canvas interior")
        xb, yb = point["pixel_xy_bottom_left"]
        require(all(type(v) is int for v in (xb, yb)) and 0 <= xb < fw and 0 <= yb < fh, "pixel address")
        require(xb == math.floor((requested[0]-dx)*sx)
                and yb == fh - 1 - math.floor((requested[1]-dy)*sy), "GL bottom-left read address")
        center = [dx + (xb+.5)/sx, dy + (fh-yb-.5)/sy]
        vector(point["pixel_center_logical"], center)
        require(inside(center, rect) == image, "actual pixel center region")
        if linked:
            require(all(abs(center[a] - crosshair[a]) >= 3 for a in (0, 1)), "sample avoids unchanged overlay")
        rgb = point["rgb"]
        require(len(rgb) == 3 and all(type(v) is int and 0 <= v <= 255 for v in rgb), "actual RGB bytes")
        if image:
            fb_rect = [(rect[0]-dx)*sx, (rect[1]-dy)*sy, (rect[2]-dx)*sx, (rect[3]-dy)*sy]
            u, v = framebuffer_uv((xb, fh-1-yb), fb_rect)
            expected = expected_gl_sample(words, width, height, u, v, mode, "repeat", "repeat")
            other = expected_gl_sample(words, width, height, u, v,
                                       "linear" if mode == "nearest" else "nearest", "repeat", "repeat")
            distinguishing += ordinal < 4 and abs(expected - other) > 8
            vector(rgb, [expected]*3, 1.)
        else:
            vector(rgb, BACKGROUND_RGB, 1.)
    require(distinguishing >= 2, "interior samples distinguish nearest from linear")


def check_owner(owner):
    require(owner["dtype"] == "uint8" and owner["sample_policy"] == "finite_only", "source sample domain")
    shape = owner["shape"]
    require(shape in ([1, 1, 1, 4, 2, 1], [1, 1, 2, 2, 2, 1]), "literal source shape")
    words = STEP_WORDS if shape[2] == 1 else CHECKER_WORDS
    require(len(owner["planes"]) == shape[2], "all fixture planes observed")
    for z, plane in enumerate(owner["planes"]):
        values = words[z*shape[3]*2:(z+1)*shape[3]*2]
        require(plane["position"] == [0, 0, z] and plane["shape"] == [shape[3], 2, 1], "plane coordinates")
        require(plane["declared_u8_hex"] == bytes(values).hex(), "literal source bytes")
        require(plane["storage_f64_hex"] == b"".join(struct.pack("<d", x) for x in values).hex(), "literal binary64 storage")
    require(owner["calibration"]["spacing"] == [.5, .5, .5], "fixture physical spacing")


def immutable_owner(owner):
    return {key: owner[key] for key in ("data_identity", "shape", "dtype", "sample_policy", "planes",
        "selection", "previous", "metadata", "calibration", "rois", "overlays", "history", "undo", "redo", "dirty")} | {
        "data_revisions": {key: owner["revisions"][key] for key in ("pixels", "metadata", "selection")}}


def check_input(report):
    frames = report["frames"]
    require(0 < len(frames) <= 750 and 0 < report["elapsed_seconds"] <= 60, "actual frame/time bounds")
    for i, frame in enumerate(frames):
        require(frame["index"] == i and frame["report"]["window_error"] == "", "frame identity and error")
        require(frame["report"]["input"]["app_focus_lost"] is False, "actual UI focus")
    endings = [e["frame_after"] for e in report["events"]]
    require(all(type(v) is int for v in endings) and endings == sorted(set(endings)), "ordered input events")
    clicked, typed = [], []
    for event in report["events"]:
        if event["kind"] == "wait_complete":
            continue
        begin, end = event["frame_before"], event["frame_after"]
        require(type(begin) is int and type(end) is int and 0 <= begin < end < len(frames), "input frame interval")
        atoms = [atom for f in frames[begin:end+1] for atom in f["raw_inputs"]]
        if event["kind"] in ("control_click", "control_hover"):
            item = event["actual_control"]
            require(type(item["id"]) is int and item["id"] > 0 and event["hovered_id"] == item["id"], "actual hovered item")
            require(inside(event["actual_mouse"], item["rect"]) and inside(event["actual_mouse"], event["actual_clip"]), "actual clickable bounds")
            if event["kind"] == "control_click":
                for down in (True, False):
                    require(any(a.get("kind") == "button" and a.get("button") == 0 and a.get("down") is down for a in atoms), "actual mouse press/release")
                clicked.append(event["control"])
            else:
                require(event["control"] == "host.menu.Image.Stacks", "fixed submenu hover route")
                hover_atoms = [a for f in frames[begin+1:end+1] for a in f["raw_inputs"]]
                require(not any(a.get("kind") == "button" for a in hover_atoms) and
                        any(a.get("kind") == "position" and a.get("point") == event["actual_mouse"] for a in hover_atoms), "actual button-free submenu hover")
                opened = event["opened_control"]
                require(opened == frames[end]["report"]["host_controls"].get("menu.action.view.orthogonal") and
                        type(opened["id"]) is int and opened["id"] > 0 and opened["visible"] is True and
                        opened["window_name"] != event["hovered_window"], "actual child submenu observation")
        elif event["kind"] == "text_input":
            require(type(event["input_id"]) is int and event["input_id"] > 0, "actual input owner")
            if event["text"]:
                require(any(a.get("kind") == "text" and a.get("text") == event["text"] for a in atoms), "actual character input")
            typed.append((event["control"], event["text"]))
        else:
            require(False, "unexpected input event")
    require("host.menu.Edit.Preferences" in clicked and "host.menu.file.open" in clicked
            and "scientific.apply" in clicked and "host.menu.action.view.orthogonal" in clicked, "real menu and owner routes")
    require(("host.tools.zoom.percent", "3200") in typed, "actual magnification input")
    if report["case_id"] == CASES[0]:
        for control in ("choice.theme.light", "field.auto_dpi", "choice.interpolation.nearest", "choice.interpolation.linear", "preferences.apply", "preferences.revert", "preferences.close_button"):
            require(control in clicked, "missing appearance action " + control)
        for entry in (("field.ui_scale", "3.5"), ("field.ui_scale", "1.5"), ("field.canvas_background", "#12xx56"),
                      ("field.canvas_background", "#123456"), ("field.default_zoom", "2")):
            require(entry in typed, "missing actual field input " + repr(entry))
    else:
        require("preferences.defaults" in clicked and "preferences.revert" in clicked, "reopened draft actions")


def checkpoint_actions(report, before, after, required):
    """Require ordered actual controls in the interval that claims their effect."""
    first, last = report["states"][before]["frame"], report["states"][after]["frame"]
    events = sorted((e for e in report["events"] if first < e["frame_after"] <= last
                     and e.get("kind") in ("control_click", "control_hover", "text_input")), key=lambda e: e["frame_after"])
    actual = [(e["control"], e.get("text")) for e in events]
    cursor = 0
    for wanted in required:
        while cursor < len(actual) and actual[cursor] != wanted:
            cursor += 1
        require(cursor < len(actual), "missing checkpoint action " + before + " → " + after + ": " + repr(wanted))
        cursor += 1


def check_report(encoded):
    report = decode_actual_observations(encoded)
    require(report["schema"] == "c2.settings-appearance.ui.001" and report["state"] == "observed"
            and report["error"] == "" and report["acceptance_claim"] is False, "successful observation envelope")
    case = report["case_id"]
    require(case in CASES and report["context"] in CONTEXTS, "fixed route identity")
    require(report["runtime_before"] == report["runtime_after"], "actual runtime stable")
    require(report["runtime_before"]["process_id"] == report["fixture"]["runtime"]["process_id"], "fixture process identity")
    require(report["fixture"]["supplied_dpi"] == 2 and report["fixture"]["spacing"] == [.5, .5, .5], "declared backend/source fixture")
    check_input(report)
    states = report["states"]
    require(set(states) == set(STATES[case]), "complete fixed state membership")
    indices = [states[name]["frame"] for name in STATES[case]]
    require(all(type(i) is int and 0 <= i < len(report["frames"]) for i in indices)
            and indices == sorted(set(indices)), "state execution order")
    initial = states["before"]
    require(len(initial["owners"]) == 1, "one initial step owner")
    step_id = initial["active_id"]
    new = states["new_owner"]
    volume_id = new["active_id"]
    require(step_id != volume_id and len(new["owners"]) == 2, "exactly one new checker owner")
    new_index = STATES[case].index("new_owner")
    originals = {}
    for ordinal, name in enumerate(STATES[case]):
        state = states[name]
        require(state["workflow"] == initial["workflow"] and state["results"] == initial["results"]
                and state["recording"] is False, "no workflow/results/recording side effects")
        require(len(state["settings"]) == 61, "complete settings map")
        require(all(state["settings"][k] == initial["settings"][k] for k in state["settings"] if k not in ACCEPTED_SETTINGS), "unrelated settings unchanged at each checkpoint")
        ids = [o["document_id"] for o in state["owners"]]
        require(len(ids) == len(set(ids)) and set(ids) == ({step_id} if ordinal < new_index else {step_id, volume_id}), "stable exact owner membership")
        for owner in state["owners"]:
            check_owner(owner)
            identity = owner["document_id"]
            originals.setdefault(identity, immutable_owner(owner))
            require(immutable_owner(owner) == originals[identity], "source/carriers/history unchanged")
        require(state["active_id"] in [o["document_id"] for o in state["owners"]], "actual active owner")
        if name in ("ordinary_nearest", "ordinary_sampled"):
            require(state["active_id"] == step_id, "ordinary sampling owns step")
        if name in ("new_owner", "linked_nearest", "linked_sampled", "linear_applied", "final_closed"):
            require(state["active_id"] == volume_id, "linked/new/final sampling owns checker")
        require(state["observations"]["window_error"] == "", "state error")
        preference_bytes(state["preferences_file"])
    initial_map = DEFAULTS if case == CASES[0] else ACCEPTED_SETTINGS
    for key, value in initial_map.items():
        require(initial["settings"][key] == value, "initial preference " + key)
    if case == CASES[0]:
        checkpoint_actions(report, "before", "draft_cancelled", [("host.menu.Edit.Preferences", None), ("choice.theme.classic", None), ("preferences.close_button", None)])
        checkpoint_actions(report, "draft_cancelled", "draft_reverted", [("field.ui_scale", "1.25"), ("preferences.revert", None)])
        checkpoint_actions(report, "draft_reverted", "invalid_scale", [("field.ui_scale", "3.5"), ("preferences.apply", None)])
        checkpoint_actions(report, "invalid_scale", "invalid_color", [("field.ui_scale", "1.5"), ("field.canvas_background", "#12xx56"), ("preferences.apply", None)])
        checkpoint_actions(report, "invalid_color", "nearest_applied", [("field.canvas_background", "#123456"), ("choice.theme.light", None), ("field.default_zoom", "2"), ("field.auto_dpi", None), ("choice.interpolation.nearest", None), ("preferences.apply", None)])
        checkpoint_actions(report, "nearest_applied", "dpi_enabled_nearest", [("field.auto_dpi", None), ("preferences.apply", None)])
        checkpoint_actions(report, "linked_nearest", "linear_applied", [("choice.interpolation.linear", None), ("preferences.apply", None)])
        require(preference_bytes(initial["preferences_file"]) is None, "fresh persist starts absent")
        for name in ("draft_cancelled", "draft_reverted", "invalid_scale", "invalid_color"):
            require(states[name]["settings"] == initial["settings"], "draft/refusal changed settings")
            require(states[name]["preferences_file"] == initial["preferences_file"], "draft/refusal wrote file")
        for name in ("invalid_scale", "invalid_color"):
            panel = states[name]["observations"]["preferences_panel"]
            key = "ui_scale" if name == "invalid_scale" else "canvas_background"
            require(key in panel["error"], "setting-specific actual validation refusal")
            require(panel["draft"]["ui_scale"] == (3.5 if name == "invalid_scale" else 1.5), "invalid scale actual draft")
            if name == "invalid_color":
                require(panel["draft"]["canvas_background"] == "#12xx56", "invalid color actual draft")
        require(states["draft_cancelled"]["observations"]["preferences_panel"]["open"] is False, "draft Close actually closed")
        reverted = states["draft_reverted"]["observations"]["preferences_panel"]["draft"]
        require(all(reverted[k] == v for k, v in DEFAULTS.items()), "actual reverted draft values")
        required_map = dict(ACCEPTED_SETTINGS, auto_dpi=False, interpolation="nearest")
        for key, value in required_map.items():
            require(states["nearest_applied"]["settings"][key] == value, "nearest accepted map")
        for name, expected in (("dpi_enabled_nearest", dict(ACCEPTED_SETTINGS, interpolation="nearest")), ("linear_applied", ACCEPTED_SETTINGS)):
            require(all(states[name]["settings"][k] == v for k, v in expected.items()), "exact phase map " + name)
        close(states["nearest_applied"]["observations"]["gui_scale"], 1.5)
        close(states["dpi_enabled_nearest"]["observations"]["gui_scale"], 3.)
        # ImGuiStyle::ScaleAllSizes truncates scaled pixel dimensions (vendored
        # imgui.cpp), so the base [7, 5] padding at 1.5 is exactly [10, 7].
        vector(states["nearest_applied"]["observations"]["appearance_runtime"]["frame_padding"], [10, 7])
        vector(states["dpi_enabled_nearest"]["observations"]["appearance_runtime"]["frame_padding"], [21, 15])
        require(states["dpi_enabled_nearest"]["observations"]["font_size"] > states["nearest_applied"]["observations"]["font_size"], "font consumer follows DPI")
    final = states["final_closed"]
    for key, value in ACCEPTED_SETTINGS.items():
        require(final["settings"][key] == value, "final applied " + key)
    require(all(final["settings"][k] == initial["settings"][k] for k in final["settings"] if k not in ACCEPTED_SETTINGS), "unrelated settings unchanged")
    require(final["observations"]["preferences_panel"]["open"] is False, "final Preferences closed")
    require(preference_bytes(final["preferences_file"]) is not None, "actual preferences saved")
    require(len(new["owners"]) == 2, "actual new owner open")
    opened = next(o for o in new["owners"] if o["document_id"] == new["active_id"])
    require(opened["shape"] == [1, 1, 2, 2, 2, 1], "new owner checker")
    close(opened["display"]["zoom"], 4.)
    vector(new["observations"]["ordinary_canvas"]["scale"], [2, 2])
    old = next(o for o in new["owners"] if o["document_id"] == initial["active_id"])
    require(old["display"] == initial["owners"][0]["display"], "default zoom does not change old camera")
    first_sample = "ordinary_nearest" if case == CASES[0] else "ordinary_sampled"
    checkpoint_actions(report, "new_owner", first_sample, [("host.menu.form.display_range", None),
        ("dialog.minimum", "0"), ("dialog.maximum", "255"), ("dialog.apply", None)])
    sampled = [("ordinary_sampled", "linear", ["ordinary"]), ("linked_sampled", "linear", ["XY", "XZ", "YZ"])]
    if case == CASES[0]:
        sampled += [("ordinary_nearest", "nearest", ["ordinary"]), ("linked_nearest", "nearest", ["XY", "XZ", "YZ"])]
    for name, mode, surfaces in sampled:
        state = states[name]
        require([s["surface"] for s in state["gl_samples"]] == surfaces, "sample surface membership")
        close(state["observations"]["gui_scale"], 3.)
        runtime = state["observations"]["appearance_runtime"]
        close(runtime["supplied_dpi"], 2.)
        require(type(runtime["actual_dpi"]) in (int, float) and runtime["actual_dpi"] > 0, "separate actual GLFW DPI")
        vector(runtime["window_background_rgba"], [.94, .94, .94, 1.])
        owner = next(o for o in state["owners"] if o["document_id"] == state["active_id"])
        volume = next(o for o in state["owners"] if o["document_id"] == volume_id)
        vector(volume["display"]["properties"]["display_range"], [0, 255])
        for sample in state["gl_samples"]:
            require(sample["runtime"] == runtime, "same-frame GL observation")
            geometry = sample["geometry"]
            if sample["surface"] == "ordinary":
                source = state["observations"]["ordinary_pixels"]
                require(geometry == state["observations"]["ordinary_canvas"], "ordinary sampled geometry is the actual state geometry")
                fixed = 0
            else:
                source = geometry
                actual_plane = next(p for p in state["observations"]["orthogonal_planes"] if p["plane"] == sample["surface"])
                require(geometry == actual_plane, "linked sampled geometry is the actual state geometry")
                axis = {"XY": 2, "XZ": 1, "YZ": 0}[sample["surface"]]
                fixed = owner["display"]["crosshair"][axis]
                require(geometry["coordinate"] == fixed, "linked source slice")
                horizontal, vertical = {"XY": (0, 1), "XZ": (0, 2), "YZ": (1, 2)}[sample["surface"]]
                cursor = owner["display"]["crosshair"]
                vector(sample["crosshair_lines_logical"], [geometry["image_rect"][0]+(cursor[horizontal]+.5)*geometry["scale"][0],
                    geometry["image_rect"][1]+(cursor[vertical]+.5)*geometry["scale"][1]])
            require(source["source_document_id"] == owner["document_id"] and source["pixels_source"] in ("live", "live_document"), "sample source ownership")
            vector(source["range"], [0, 255])
            check_gl_sample(sample, mode, fixed_index=fixed)
    if case == CASES[0]:
        for near, linear in (("ordinary_nearest", "ordinary_sampled"), ("linked_nearest", "linked_sampled")):
            for before, after in zip(states[near]["gl_samples"], states[linear]["gl_samples"]):
                require(before["geometry"]["image_rect"] == after["geometry"]["image_rect"], "filter comparison holds camera")
                require(before["sampler"]["texture_id"] == after["sampler"]["texture_id"], "cached texture updated in place")
                require(any(a["rgb"] != b["rgb"] for a, b in zip(before["points"][:4], after["points"][:4])), "actual filter pixels change")
    else:
        checkpoint_actions(report, "before", "shown", [("host.menu.Edit.Preferences", None)])
        shown = states["shown"]["observations"]["preferences_panel"]
        require(shown["open"] is True and all(shown["draft"][k] == v for k, v in ACCEPTED_SETTINGS.items()), "reopened Preferences displays loaded values")
        checkpoint_actions(report, "linked_sampled", "defaults_draft", [("group.Appearance", None), ("preferences.defaults", None)])
        checkpoint_actions(report, "defaults_draft", "reverted", [("preferences.revert", None)])
        require(all(s["settings"] == initial["settings"] and s["preferences_file"] == initial["preferences_file"] for s in states.values()), "reopen drafts leave persistence unchanged")
        draft = states["defaults_draft"]["observations"]["preferences_panel"]["draft"]
        require(all(draft[k] == v for k, v in DEFAULTS.items()), "Defaults edits actual draft")
        reverted = states["reverted"]["observations"]["preferences_panel"]["draft"]
        require(all(reverted[k] == v for k, v in ACCEPTED_SETTINGS.items()), "Revert restores loaded draft")
    return report


def check_pair(persist, reopen):
    first, second = check_report(persist), check_report(reopen)
    require((first["case_id"], second["case_id"]) == CASES and first["context"] == second["context"], "ordered same-launcher pair")
    require(first["runtime_before"]["process_id"] != second["runtime_before"]["process_id"], "fresh process reopen")
    require(first["states"]["final_closed"]["preferences_file"] == second["states"]["before"]["preferences_file"], "same byte-exact actual saved preferences")
    require(first["states"]["final_closed"]["settings"] == second["states"]["before"]["settings"], "fresh process loaded complete map")
    return first, second
