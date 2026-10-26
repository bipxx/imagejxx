"""Independent loaded-FPS clock checks; no product imports or process launch.

This establishes file-loaded FPS consumption, not the Preferences widget chain.
Existing animation observations and their historical expectations stay intact.
"""
import math
import struct


def check_loaded_fps(report, fps, context):
    def require(value, message):
        if not value:
            raise AssertionError("C5 loaded FPS: " + message)

    require(type(fps) in (float, int) and fps in (5, 20), "fixed independent rate")
    case = report["case_id"]
    require(case in ("fps-loaded", "cal-loaded") and context in ("imgui_cpp", "imgui_python"), "fixed route")
    calibrated = case == "cal-loaded"
    shape = [5 if calibrated else 2, 3, 4, 5, 7, 1]
    axis, axis_index, extent = ("t", 0, 5) if calibrated else ("z", 2, 4)
    interval = .05 if calibrated else 1 / fps
    require(report["contract_id"] == "p09s.animation-ui.001" and report["context"] == context and
            report["observation_state"] == "observed" and report["acceptance_claim"] is False, "observation envelope")
    require(report["loaded_fps"] == fps and report["shape"] == shape, "loaded setting and fixture")
    frames = report["frames"]
    require(report["frame_count"] == len(frames) <= 180 and 0 < report["elapsed_seconds"] <= 15, "bounds")
    require(report["lifecycle"]["open"] is False and report["api"] == [], "closed session without setting API substitution")
    before, after = report["before"], report["after"]
    require(before["position"] == [1, 1, 1] and before["crosshair"] == [3, 2, 1], "literal initial position")
    samples = [struct.unpack("<I", struct.pack("<f", 1000*t+100*c+10*z+y+x))[0]
               for t in range(shape[0]) for c in range(3) for z in range(4)
               for y in range(5) for x in range(7)]
    require(before["samples"] == after["samples"] == samples, "literal source words")
    invariant = ("source_id", "data_identity", "title", "properties", "selection", "previous", "roi",
                 "crosshair", "zoom", "plane_zoom", "linked_zoom", "orthogonal", "spacing", "origin",
                 "frame_interval", "time_unit")
    # Only the selected display coordinate may move; ordinary XYZ remains
    # separate from scientific T. Compare each remaining component explicitly.
    invariant = tuple(k for k in invariant if k != "crosshair")
    previous = before
    ticks = 0
    anchor = None
    positions = []
    for frame in frames:
        rendered, source = frame["rendered"], frame["source"]
        animation = rendered["animation"]
        require(frame["events"] == rendered["input_frame"]["raw_inputs"] and not rendered["window_error"], "actual settled frame")
        require(animation["fps"] == fps and animation["clock"] == "imgui_monotonic_seconds", "loaded native clock consumer")
        delta = animation["ticks"] - ticks
        require(delta in (0, 1), "one advance per sampled frame")
        if animation["playing"] or ticks:
            require(animation["axis"] == axis and animation["timing"] == ("calibrated_time" if calibrated else "fps") and
                    math.isclose(animation["interval_seconds"], interval, rel_tol=0, abs_tol=1e-12), "independent timing policy")
        if delta:
            require(anchor is not None and animation["last_tick"] - anchor >= interval - 1e-9, "no early tick")
            require(animation["last_tick"] - anchor <= interval + 1/60 + 1e-6, "next deterministic frame advances")
            expected = list(previous["position"])
            expected[axis_index] = (expected[axis_index] + 1) % extent
            require(source["position"] == expected, "literal forward wrap")
            positions.append(expected[axis_index])
        else:
            require(source["position"] == previous["position"], "no unclocked movement")
        if animation["playing"]:
            require(math.isfinite(animation["last_tick"]), "finite actual clock anchor")
            # The menu action runs after this frame's playback update. Its
            # initial -1 sentinel is armed on the next frame, before any tick.
            if animation["last_tick"] < 0:
                require(animation["last_tick"] == -1 and ticks == 0 and delta == 0 and anchor is None,
                        "only initial menu activation may await its clock anchor")
            else:
                anchor = animation["last_tick"]
            panes = rendered["orthogonal_planes"]
            require(len(panes) == 3 and {p["plane"] for p in panes} == {"XY", "XZ", "YZ"}, "complete linked views")
            for pane in panes:
                require(pane["current_tcz"] == source["position"], "linked view follows source position")
        require(source["display_revision"] == previous["display_revision"] + delta, "display-only publication")
        require(source["shares_original"] and not source["dirty"] and source["undo"] == source["redo"] == 0 and
                source["pixels_revision"] == source["metadata_revision"] == source["selection_revision"] == 0 and
                source["workflow"] == source["results"] == [], "source/history/workflow preservation")
        require(all(source[k] == before[k] for k in invariant), "unrelated native state")
        require(source["crosshair"][:2] == before["crosshair"][:2] and
                (axis == "z" or source["crosshair"] == before["crosshair"]), "unrelated crosshair")
        previous, ticks = source, animation["ticks"]
    expected_positions = [2, 3, 4, 0, 1, 2, 3, 4] if calibrated else [2, 3, 0, 1, 2, 3, 0]
    require(positions == expected_positions, "complete literal playback trace")
    require(all(after[k] == v for k, v in previous.items()), "final source matches settled frame")
    require(report["animation"]["playing"] is False and report["animation"]["source_id"] is None, "normal stop releases source")
    return {"case": case, "fps": fps, "context": context, "frames": len(frames), "positions": positions}
