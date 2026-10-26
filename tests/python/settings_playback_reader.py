"""C5 actual Preferences/clock reader. No product import, launcher or timer."""
import hashlib
import json
import math
import struct
from actual_observation_transport import decode_actual_observations

CASES = ("playback-persist", "playback-reopen")
CONTEXTS = ("imgui_cpp", "imgui_python")
PERSIST = ("initial", "shown", "cancel_draft", "cancelled", "cancel_reopened", "reverted", "invalid_low", "invalid_high",
           "five_draft", "five_applied", "five_playing", "twenty_draft_playing", "draft_clock",
           "twenty_applied_playing", "twenty_playing", "fps_stopped", "defaults_draft",
           "defaults_reverted", "calibrated_playing", "final")
REOPEN = ("initial", "shown", "twenty_playing", "fps_stopped", "defaults_draft", "defaults_reverted", "calibrated_playing", "final")


def require(value, message):
    if not value:
        raise AssertionError("C5: " + message)


def inside(point, rect):
    return rect[0] <= point[0] < rect[2] and rect[1] <= point[1] < rect[3]


def check_draft(state, value):
    panel = state["preferences_panel"]
    require(panel["open"] is True, "actual Preferences is open")
    field = panel["fields"]["animation_fps"]
    require(type(field["draft"]) in (int, float) and field["draft"] == value and
            field["applied"] == state["settings"]["animation_fps"], "actual draft/applied FPS")


def payload(file):
    raw = file["utf8"].encode("utf8")
    require(0 < len(raw) == file["bytes"] <= 1048576 and hashlib.sha256(raw).hexdigest() == file["sha256"], "original file bytes/hash")
    return json.loads(raw)


def opaque(preferences, known):
    extras = preferences.get("extras", {})
    require(type(extras) is dict and extras.get("recent", []) == [] and
            type(extras.get("recent", [])) is list, "only absent/empty reserved recent list")
    return ({k: v for k, v in preferences.items() if k not in ("format", "version", "values", "extras")},
            {k: v for k, v in preferences["values"].items() if k not in known},
            {k: v for k, v in extras.items() if k != "recent"})


def check_input(report):
    frames = report["frames"]
    previous_end = -1
    for event in report["events"]:
        begin, end = event["frame_before"], event["frame_after"]
        require(type(begin) is int and type(end) is int and 0 <= begin < end < len(frames) and end > previous_end, "ordered actual input intervals")
        previous_end = end
        atoms = [a for f in frames[begin+1:end+1] for a in f["events"]]
        if event["kind"] in ("control_click", "control_hover"):
            item = event["actual_control"]
            require(type(item["id"]) is int and item["id"] > 0 and event["hovered_id"] == item["id"] and
                    inside(event["actual_mouse"], item["rect"]), "real hovered item")
            if event["kind"] == "control_click":
                require(inside(event["actual_mouse"], event["actual_clip"]), "actual clip")
                for down in (True, False):
                    require(any(a.get("type") == "button" and a.get("name") == "left" and a.get("down") is down for a in atoms), "press/release input")
            else:
                require(event["control"] == "host.menu.Image.Stacks" and not any(a.get("type") == "button" for a in atoms) and
                        any(a.get("type") == "position" and a["point"] == item["point"] for a in atoms), "actual submenu hover")
                require("menu.action.animation" in frames[end]["report"]["host_controls"], "opened playback menu")
        elif event["kind"] == "text_input":
            require(type(event["input_id"]) is int and event["input_id"] > 0 and
                    any(a.get("type") == "text" and a.get("value") == event["text"] for a in atoms), "actual focused character input")
            begin_frame = next((f for f in frames[begin+1:end+1]
                                if f["label"] == event["control"] + ".select_all"), None)
            require(begin_frame is not None and begin_frame["report"]["input_frame"]["focus"]["active_id"] == event["input_id"], "keyboard ownership")
        else:
            require(False, "unknown input event")
    clicks = [e["control"] for e in report["events"] if e["kind"] == "control_click"]
    for control in ("host.menu.Edit.Preferences", "host.menu.action.animation", "host.menu.animation.axis.t",
                    "dialog.timing.choice.Calibrated time", "preferences.defaults", "preferences.revert"):
        require(control in clicks, "missing actual route " + control)


def check_source(source, baseline):
    require(set(source) == set(baseline), "complete source shape")
    for key in set(baseline) - {"display", "revisions"}:
        require(source[key] == baseline[key], "unchanged source " + key)
    require(source["shares_original"] is True and source["dirty"] is False and source["undo"] == source["redo"] == 0, "no data history")
    for key in ("pixels", "metadata", "selection"):
        require(source["revisions"][key] == baseline["revisions"][key], "unchanged revision " + key)
    for key in set(baseline["display"]) - {"t", "c", "crosshair", "revision"}:
        require(source["display"][key] == baseline["display"][key], "unchanged display " + key)
    require(source["display"]["crosshair"][:2] == [2, 1] and source["display"]["c"] == 1, "unrelated camera/channel")


def position(source):
    d = source["display"]
    return [d["t"], d["c"], d["crosshair"][2]]


def check_clock(report, baseline):
    frames = report["frames"]
    first = report["states"]["initial"]["frame"]
    previous = frames[first]["report"]
    moves = {"z": 0, "t": 0}
    for frame in frames[first+1:]:
        current = frame["report"]
        source, old_source = current["source"], previous["source"]
        check_source(source, baseline)
        a, old = current["animation"], previous["animation"]
        now, before = current["clock_seconds"], previous["clock_seconds"]
        require(math.isfinite(now) and now > before and math.isclose(now-before, current["delta_seconds"], rel_tol=0, abs_tol=1e-9)
                and math.isclose(current["delta_seconds"], 1/60, rel_tol=0, abs_tol=1e-9), "actual deterministic clock")
        require(a["clock"] == "imgui_monotonic_seconds" and a["fps"] == current["fps"], "native applied FPS")
        require(a["timing"] in ("fps", "calibrated_time"), "timing choice")
        interval = 1/current["fps"] if a["timing"] == "fps" else .05
        require(math.isclose(a["interval_seconds"], interval, rel_tol=0, abs_tol=1e-12), "independent interval")
        # Delivered UI Apply happens after the frame's advance. The previous
        # sampled rate/timing determines this update; subsequent frames use new.
        due_interval = 1/previous["fps"] if old["timing"] == "fps" else .05
        expected_tick = bool(old["playing"] and old["last_tick"] >= 0 and now-old["last_tick"] >= due_interval)
        expected_position = position(old_source)
        if expected_tick:
            require(old["axis"] in ("z", "t"), "selected playback axis")
            axis, extent = (2, 4) if old["axis"] == "z" else (0, 3)
            expected_position[axis] = (expected_position[axis]+1) % extent
            moves[old["axis"]] += 1
        require(position(source) == expected_position, "literal due/wrap position")
        require(source["display"]["revision"] == old_source["display"]["revision"] + int(expected_tick) and
                source["revisions"]["display"] == old_source["revisions"]["display"] + int(expected_tick), "display-only tick")
        starting = a["playing"] and not old["playing"]
        require(a["ticks"] == (0 if starting else old["ticks"]+int(expected_tick)), "tick counter/lifetime")
        if a["playing"]:
            require(a["source_id"] == baseline["document_id"], "captured source owner")
            panes = current["orthogonal_planes"]
            require(len(panes) == 3 and {p["plane"] for p in panes} == {"XY", "XZ", "YZ"} and
                    all(p["current_tcz"] == position(source) for p in panes), "complete linked TCZ consumers")
        else:
            require(a["source_id"] is None, "paused source released")
        if current["fps"] != previous["fps"]:
            require(frame["label"] == "preferences.apply.release", "FPS changes only on actual Apply release")
        previous = current
    require(moves["z"] >= 5 and moves["t"] >= 6, "both actual consumers exercised")


def check_report(encoded, context, case):
    report = decode_actual_observations(encoded)
    require(case in CASES and context in CONTEXTS and (report["case_id"], report["context"]) == (case, context), "fixed route")
    require(report["schema"] == "c5.settings-playback.ui.001" and report["state"] == "observed" and report["error"] == "" and report["acceptance_claim"] is False, "observation envelope")
    require(report["runtime_before"] == report["runtime_after"] and report["lifecycle"]["open"] is False, "runtime/lifetime")
    require(0 < len(report["frames"]) <= 750 and 0 < report["elapsed_seconds"] <= 60, "bounds")
    states = report["states"];names = PERSIST if case == CASES[0] else REOPEN
    require(set(states) == set(names) and [states[n]["frame"] for n in names] == sorted(set(states[n]["frame"] for n in names)), "complete ordered states")
    initial = states["initial"];baseline = initial["source"]
    words = [struct.unpack("<I", struct.pack("<f", 1000*t+100*c+10*z+y+x))[0]
             for t in range(3) for c in range(2) for z in range(4) for y in range(3) for x in range(5)]
    require(baseline["shape"] == [3,2,4,3,5,1] and baseline["dtype"] == "float32" and baseline["samples"] == words and position(baseline) == [1,1,1], "literal source fixture")
    require(baseline["metadata"] == {"title":"C5 playback", "properties":{"c5":"retained property"},
            "extensions":{"c5":"retained extension"}, "record_layout":{"c5":"retained layout"}}, "literal opaque source")
    known = set(initial["settings"]);require(len(known) == 61, "complete settings")
    expected_opaque = ({"c5_future_root":{"opaque":["retain",17]}}, {"c5_future_value":{"raw":[1,"two",True]}}, {"c5":"retain extra"})
    first_fps = 8 if case == CASES[0] else 20
    require(initial["settings"]["animation_fps"] == first_fps, "constructor loaded value")
    for name in names:
        state = states[name];check_source(state["source"], baseline)
        frame = report["frames"][state["frame"]]["report"]
        require(state["source"] == frame["source"] and state["animation"] == frame["animation"] and
                state["settings"]["animation_fps"] == frame["fps"], "checkpoint matches its actual frame")
        fps = (8 if names.index(name)<names.index("five_applied") else 5 if names.index(name)<names.index("twenty_applied_playing") else 20) if case == CASES[0] else 20
        require(set(state["settings"]) == known and state["settings"]["animation_fps"] == fps and
                all(state["settings"][k] == initial["settings"][k] for k in known-{"animation_fps"}), "only accepted FPS published")
        require(opaque(state["preferences"], known) == opaque(payload(state["preferences_file"]), known) == expected_opaque, "unknown preference compartments")
        expected_file = initial["preferences_file"] if fps == first_fps and (case == CASES[1] or names.index(name)<names.index("five_applied")) else states["five_applied" if fps == 5 else "twenty_applied_playing"]["preferences_file"]
        require(state["preferences_file"] == expected_file, "only Apply rewrites file")
        if fps != 8:
            require(payload(state["preferences_file"])["values"]["animation_fps"] == fps, "saved applied FPS")
        require(state["workflow"] == state["results"] == [] and state["recording"] is False and state["error"] == "", "no workflow/history side effects")
    for name, draft in (("defaults_draft",8), ("defaults_reverted",20)):
        check_draft(states[name], draft)
        panel = states[name]["preferences_panel"]
        require(panel["open"] and panel["group"] == "Playback" and panel["fields"]["animation_fps"]["draft"] == draft, "group Defaults/Revert draft")
    if case == CASES[0]:
        require(states["cancelled"]["preferences_panel"]["open"] is False, "actual Close cancels dialog")
        for name, draft in (("shown",8),("cancel_draft",5),("cancel_reopened",8),("reverted",8),
                            ("invalid_low",0),("invalid_high",121),("five_draft",5),("twenty_draft_playing",20)):
            check_draft(states[name], draft)
            panel = states[name]["preferences_panel"]
            require(panel["fields"]["animation_fps"]["draft"] == draft, "actual FPS draft")
            if name.startswith("invalid_"):
                require("animation_fps" in panel["error"] and "range" in panel["error"], "setting-specific refusal")
        require(states["draft_clock"]["animation"]["ticks"] >= states["twenty_draft_playing"]["animation"]["ticks"]+2 and
                states["draft_clock"]["animation"]["fps"] == 5, "draft does not change playing clock")
    else:
        check_draft(states["shown"],20)
    require(not states["final"]["animation"]["playing"] and states["calibrated_playing"]["animation"]["timing"] == "calibrated_time", "actual calibrated mode and Stop")
    for i, frame in enumerate(report["frames"]):
        require(frame["ordinal"] == i and frame["events"] == frame["report"]["input_frame"]["raw_inputs"] and
                not frame["report"]["window_error"] and not frame["report"]["input_frame"]["app_focus_lost"], "settled actual frame")
    check_input(report);check_clock(report, baseline)
    return report


def check_pair(persist, reopen):
    require(persist["case_id"] == CASES[0] and reopen["case_id"] == CASES[1] and persist["context"] == reopen["context"], "paired route/context")
    require(persist["runtime_before"]["process_id"] != reopen["runtime_before"]["process_id"], "fresh reopen process")
    require(persist["states"]["final"]["preferences_file"] == reopen["states"]["initial"]["preferences_file"], "exact UI-saved handoff")
    require(persist["states"]["final"]["settings"] == reopen["states"]["initial"]["settings"], "all settings loaded")
    return persist, reopen
