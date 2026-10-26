"""Synthetic framebuffer protocol controls; no actual GL qualification."""
from copy import deepcopy
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tests/python"))
import settings_appearance_reader as reader


def sample(name="ordinary", mode="linear", *, cursor=(0, 0, 0)):
    linked = name != "ordinary"
    rect = [60., 70., 124., 134. if linked else 198.]
    axes = {"XY": (0, 1, 2), "XZ": (0, 2, 1), "YZ": (1, 2, 0)}
    crosshair = ([rect[a]+(cursor[axes[name][a]]+.5)*32 for a in (0, 1)] if linked else None)
    fixed = cursor[axes[name][2]] if linked else 0
    sampler = {"available": True, "texture_id": 7, "min_filter": 9729 if mode == "linear" else 9728,
               "mag_filter": 9729 if mode == "linear" else 9728, "wrap_s": 10497, "wrap_t": 10497,
               "width": 2, "height": 2 if linked else 4}
    result = {"surface": name, "geometry": {"visible": True, "loading": False, "width": 2,
              "height": sampler["height"], "scale": [32, 32], "sampler": sampler,
              "image_rect": rect, "canvas_rect": [0, 0, 300, 300]}, "sampler": sampler,
              "crosshair_lines_logical": crosshair,
              "runtime": {"framebuffer_size": [800, 600], "viewport": [0, 0, 800, 600],
                          "framebuffer_scale": [1.25, 1.5], "display_pos": [-10, -20],
                          "vendor": "SYNTHETIC", "renderer": "SYNTHETIC", "version": "SYNTHETIC",
                          "shading_language_version": "SYNTHETIC"}, "points": []}
    bases = [(u, v) for v in (.375, .625) for u in (.375, .625)] + [(.25, .25), (.75, .75), None]
    for i, base in enumerate(bases):
        offset = [0, 0]
        requested = [12, 12] if base is None else [rect[a]+base[a]*(rect[a+2]-rect[a]) for a in (0, 1)]
        if linked and i in (4, 5):
            for a in (0, 1):
                if abs(requested[a]-crosshair[a]) < 3:
                    offset[a] = 4 if requested[a] < (rect[a]+rect[a+2])*.5 else -4
                    requested[a] += offset[a]
        x, top = math.floor((requested[0]+10)*1.25), math.floor((requested[1]+20)*1.5)
        center = [-10+(x+.5)/1.25, -20+(top+.5)/1.5]
        if base is None:
            rgb = [18, 52, 86]
        else:
            u, v = ((center[0]-rect[0])/(rect[2]-rect[0]), (center[1]-rect[1])/(rect[3]-rect[1]))
            # Independent explicit half-texel fixture equation for this interior.
            if mode == "nearest":
                value = (128*((int(u*2)+int(v*2)+fixed) % 2)) if linked else 128*int(u*2)
            else:
                phase_x, phase_y = (u*2-.5) % 2, (v*2-.5) % 2
                fx, fy = min(phase_x, 2-phase_x), min(phase_y, 2-phase_y)
                value = 128*(fx+fy-2*fx*fy) if linked else 128*fx
                if fixed:
                    value = 128-value
            rgb = [round(value)]*3
        result["points"].append({"role": "background" if base is None else "image",
            "sample_kind": "interior" if i < 4 else "endpoint_near" if base else "background",
            "base_uv": list(base) if base else None, "avoidance_offset_logical": offset,
            "requested_logical": requested, "pixel_xy_bottom_left": [x, 599-top],
            "pixel_center_logical": center, "rgb": rgb})
    return result


# Literal synthetic settings/source/recipe inputs, independent of reader constants.
# These reports exercise protocol validation only; none is a product observation.
DEFAULT_SETTINGS = {
    "theme": "dark", "ui_scale": 1., "auto_dpi": True, "canvas_background": "#181b21",
    "default_zoom": 0., "interpolation": "nearest", "foreground": "#ffffff", "background": "#000000",
    "brush_width": 8., "rounded_corner_diameter": 8, "line_width": 1., "point_size": 8., "text_size": 16,
    "show_rois": True, "selection_enlargement_distance": 15., "selection_enlargement_units": "calibrated",
    "wand_tolerance": 0., "wand_connectivity": 4, "animation_fps": 8., "conversion_scale": True,
    "conversion_weighted_rgb": False, "conversion_red_weight": .299, "conversion_green_weight": .587,
    "conversion_blue_weight": .114, "conversion_range_policy": "display", "conversion_full_range_inversions": False,
    "conversion_unsigned16_range": "auto", "histogram_custom": False, "histogram_bins": 256,
    "histogram_lower": 0., "histogram_upper": 1., "histogram_log": False, "histogram_use_roi": True,
    "histogram_scientific": False, "histogram_roi": "active_selection", "threshold_method": "default",
    "threshold_histogram_roi": False, "threshold_apply_roi": "active_selection", "threshold_stack_histogram": False,
    "threshold_bright": True, "threshold_ignore_black": False, "threshold_ignore_white": False,
    "binary_iterations": 1, "binary_neighbors": 1, "binary_black_background": True, "binary_pad_edges": False,
    "history_limit": 8, "history_memory_mb": 0, "async_viewport": False, "viewport_cache_mb": 128,
    "render_memory_mb": 1024, "processing_workers": 2, "processing_memory_mb": 512,
    "measurement_precision": -1, "measurements": [], "profile_samples": 0, "print_dpi": 150.,
    "print_landscape": False, "startup_mode": "empty", "startup_workspace": "", "shortcut_bindings": [],
}
APPLIED = {"theme": "light", "ui_scale": 1.5, "auto_dpi": True,
           "canvas_background": "#123456", "default_zoom": 2., "interpolation": "linear"}
PREFERENCES = [("host.menu.Edit", None), ("host.menu.Edit.Preferences", None)]
OPEN_VOLUME = [("preferences.close_button", None), ("host.menu.File", None), ("host.menu.file.open", None),
               ("host.browser.path", "/synthetic/c2/checker.tif"), ("host.browser.submit", None),
               ("scientific.confirm_axes", None), ("scientific.apply", None)]
RANGE_AND_STEP = [("host.tools.zoom.percent", "3200"), ("host.menu.Image", None),
                  ("host.menu.form.display_range", None), ("dialog.minimum", "0"), ("dialog.maximum", "255"),
                  ("dialog.apply", None), ("document.step", None), ("host.tools.zoom.percent", "3200")]
LINK_VOLUME = [("document.volume", None), ("host.menu.Image", None), ("host.menu.Image.Stacks", None),
               ("host.menu.action.view.orthogonal", None)]
PERSIST_FLOW = [
    ("before", []),
    ("draft_cancelled", PREFERENCES + [("choice.theme.classic", None), ("preferences.close_button", None)]),
    ("draft_reverted", PREFERENCES + [("field.ui_scale", "1.25"), ("preferences.revert", None)]),
    ("invalid_scale", [("field.ui_scale", "3.5"), ("preferences.apply", None)]),
    ("invalid_color", [("field.ui_scale", "1.5"), ("field.canvas_background", "#12xx56"), ("preferences.apply", None)]),
    ("nearest_applied", [("field.canvas_background", "#123456"), ("choice.theme.light", None),
                         ("field.default_zoom", "2"), ("field.auto_dpi", None),
                         ("choice.interpolation.nearest", None), ("preferences.apply", None)]),
    ("dpi_enabled_nearest", [("field.auto_dpi", None), ("preferences.apply", None)]),
    ("new_owner", OPEN_VOLUME), ("ordinary_nearest", RANGE_AND_STEP), ("linked_nearest", LINK_VOLUME),
    ("linear_applied", PREFERENCES + [("choice.interpolation.linear", None), ("preferences.apply", None)]),
    ("ordinary_sampled", [("preferences.close_button", None), ("document.step", None)]),
    ("linked_sampled", [("document.volume", None)]), ("final_closed", []),
]
REOPEN_FLOW = [
    ("before", []), ("shown", PREFERENCES), ("new_owner", OPEN_VOLUME),
    ("ordinary_sampled", RANGE_AND_STEP), ("linked_sampled", LINK_VOLUME),
    ("defaults_draft", PREFERENCES + [("preferences.search", ""), ("preferences.group", None),
                                     ("group.Appearance", None), ("preferences.defaults", None)]),
    ("reverted", [("preferences.revert", None)]), ("final_closed", [("preferences.close_button", None)]),
]


def preference_file(settings):
    raw = None if settings is None else json.dumps({"format": "imagejxx-preferences", "version": 1,
        "values": settings, "extras": {"recent": []}}, sort_keys=True, separators=(",", ":"))
    return {"path": "/synthetic/c2/preferences.json", "exists": raw is not None,
            "utf8": raw, "byte_count": len(raw.encode()) if raw else 0,
            "sha256": hashlib.sha256(raw.encode()).hexdigest() if raw else None}


def source_owner(identity, *, checker=False):
    planes = ([0, 128, 128, 0], [128, 0, 0, 128]) if checker else ([0, 128]*4,)
    height = 2 if checker else 4
    return {"document_id": identity, "data_identity": identity+1000, "scientific": checker,
        "shape": [1, 1, len(planes), height, 2, 1], "dtype": "uint8", "sample_policy": "finite_only",
        "planes": [{"position": [0, 0, z], "shape": [height, 2, 1], "declared_u8_hex": bytes(words).hex(),
                    "storage_f64_hex": b"".join(struct.pack("<d", x) for x in words).hex()}
                   for z, words in enumerate(planes)], "selection": None, "previous": None,
        "metadata": {"title": "C2 checker" if checker else "C2 step", "properties": {},
                     "extensions": {}, "record_layout": {}},
        "calibration": {"spacing": [.5, .5, .5], "origin": [0, 0, 0], "direction": [1, 0, 0, 0, 1, 0, 0, 0, 1],
                        "unit": "um", "frame_interval": 1., "time_origin": 0., "time_unit": "s"},
        "rois": [], "overlays": [], "dirty": False, "undo": 0, "redo": 0,
        "history": {"undo_steps": 0, "redo_steps": 0, "step_limit": 8, "pixel_budget": 0,
                    "pixels": {"logical_bytes": 64, "current_bytes": 64, "retained_bytes": 0,
                               "obsolete_bytes": 0, "retained_planes": 0, "exact": True}},
        "revisions": {"pixels": 0, "metadata": 0, "selection": 0, "display": 1},
        "display": {"t": 0, "c": 0, "crosshair": [1, height//2, 0], "orthogonal": False, "linked_zoom": True,
                    "zoom": 4. if checker else 16., "plane_zoom": [1, 1, 1], "pan": [[0, 0]]*3,
                    "revision": 1, "properties": {} if checker else {"display_range": [0, 255]}}}


def full_report(case="appearance-persist", context="imgui_cpp"):
    persist = case == "appearance-persist"
    flow = PERSIST_FLOW if persist else REOPEN_FLOW
    names = [name for name, _ in flow]
    pid, step_id, volume_id = (101, 11, 12) if persist else (102, 21, 22)
    runtime = {"process_id": pid, "parent_process_id": 100, "producer_source": "SYNTHETIC",
               "imgui": "SYNTHETIC", "pointer_bits": 64}
    for role in ("executable", "producer_artifact", "native_artifact"):
        artifact = "application" if context == "imgui_cpp" else "python" if role == "executable" else "_desktop"
        runtime[role] = {"path": "/synthetic/c2/"+artifact, "bytes": 9,
                         "sha256": hashlib.sha256(b"SYNTHETIC").hexdigest()}
    report = {"schema": "c2.settings-appearance.ui.001", "case_id": case, "context": context,
        "state": "observed", "error": "", "acceptance_claim": False, "synthetic_protocol_only": True,
        "runtime_before": deepcopy(runtime), "runtime_after": deepcopy(runtime), "elapsed_seconds": 5.,
        "fixture": {"runtime": {"process_id": pid, "parent_process_id": 100}, "supplied_dpi": 2.,
                    "spacing": [.5, .5, .5], "display_range": [0, 255],
                    "step": {"shape": [1, 1, 1, 4, 2, 1], "rows": [[0, 128]]*4},
                    "checker": {"shape": [1, 1, 2, 2, 2, 1], "planes": [[[0, 128], [128, 0]], [[128, 0], [0, 128]]]}},
        "setup_events": [], "frames": [], "events": [], "states": {}, "milestones": {},
        "bounds": {"frames": 750, "seconds": 60, "report_bytes_exclusive": 16*1024*1024,
                   "preferences_bytes": 1024*1024, "pixels_per_surface": 7}}

    def frame(atoms=()):
        report["frames"].append({"index": len(report["frames"]), "raw_inputs": list(atoms),
            "report": {"window_error": "", "input": {"app_focus_lost": False}}})

    frame()
    for ordinal, (name, actions) in enumerate(flow):
        for control, text in actions:
            # Match the producer's real input shape: focus click, then characters.
            for kind in (("control_click",) if text is None else ("control_click", "text_input")):
                begin, item_id = len(report["frames"])-1, len(report["events"])+1
                if kind == "control_click":
                    frame([{"kind": "button", "button": 0, "down": True}])
                    frame([{"kind": "button", "button": 0, "down": False}])
                    event = {"actual_control": {"id": item_id, "rect": [20, 20, 40, 40]},
                             "hovered_id": item_id, "actual_mouse": [30, 30], "actual_clip": [0, 0, 300, 300]}
                else:
                    frame([{"kind": "text", "text": text}]); frame()
                    event = {"input_id": item_id, "text": text}
                report["events"].append(dict(event, kind=kind, control=control,
                    frame_before=begin, frame_after=len(report["frames"])-1))
        frame()
        settings = deepcopy(DEFAULT_SETTINGS)
        if not persist or ordinal >= names.index("nearest_applied"):
            settings.update(APPLIED)
            if persist and ordinal < names.index("linear_applied"):
                settings["interpolation"] = "nearest"
            if name == "nearest_applied":
                settings["auto_dpi"] = False
        draft, error = deepcopy(settings), ""
        if name in ("invalid_scale", "invalid_color"):
            draft["ui_scale"] = 3.5 if name == "invalid_scale" else 1.5
            error = "ui_scale: outside range" if name == "invalid_scale" else "canvas_background: invalid color"
            if name == "invalid_color":
                draft["canvas_background"] = "#12xx56"
        if name == "defaults_draft":
            draft = deepcopy(DEFAULT_SETTINGS)
        owners = [source_owner(step_id)]
        opened = ordinal >= names.index("new_owner")
        if opened:
            owners.append(source_owner(volume_id, checker=True))
        if ordinal > names.index("new_owner"):
            for owner in owners:
                owner["display"].update(zoom=64., revision=2)
                owner["display"]["properties"]["display_range"] = [0, 255]
                owner["revisions"]["display"] = 2
        active = step_id if not opened or name.startswith("ordinary_") else volume_id
        gui_scale = settings["ui_scale"]*(2 if settings["auto_dpi"] else 1)
        gl_runtime = dict(sample()["runtime"], supplied_dpi=2., actual_dpi=1.5,
                          window_background_rgba=[.94, .94, .94, 1.],
                          frame_padding=[math.trunc(7*gui_scale), math.trunc(5*gui_scale)])
        closed = name in ("before", "draft_cancelled", "new_owner", "ordinary_nearest", "linked_nearest",
                          "ordinary_sampled", "linked_sampled", "final_closed")
        observations = {"window_error": "", "gui_scale": gui_scale, "font_size": 13*gui_scale,
            "appearance_runtime": gl_runtime, "preferences_panel": {"open": not closed, "draft": draft, "error": error},
            "ordinary_canvas": {"scale": [2, 2] if name == "new_owner" else [32, 32]},
            "ordinary_pixels": {"source_document_id": active, "pixels_source": "live", "range": [0, 255]},
            "orthogonal_planes": []}
        surfaces = ["ordinary"] if name.startswith("ordinary_") else ["XY", "XZ", "YZ"] if name.startswith("linked_") else []
        samples = []
        for surface in surfaces:
            item = sample(surface, settings["interpolation"], cursor=(1, 1, 0))
            item["runtime"] = deepcopy(gl_runtime)
            if surface == "ordinary":
                observations["ordinary_canvas"] = deepcopy(item["geometry"])
            else:
                fixed = {"XY": 0, "XZ": 1, "YZ": 1}[surface]
                item["geometry"].update(plane=surface, coordinate=fixed, source_document_id=volume_id,
                                        pixels_source="live_document", range=[0, 255])
                observations["orthogonal_planes"].append(deepcopy(item["geometry"]))
                owners[1]["display"]["orthogonal"] = True
            samples.append(item)
        saved = settings if not persist or ordinal >= names.index("nearest_applied") else None
        report["states"][name] = {"frame": len(report["frames"])-1, "settings": settings,
            "preferences_file": preference_file(saved), "active_id": active, "owners": owners,
            "workflow": [], "results": [], "recording": False, "observations": observations, "gl_samples": samples}
    return report


class FramebufferReader(unittest.TestCase):
    def test_checkpoint_actions_cannot_borrow_an_apply_from_another_interval(self):
        report = {"states": {"before": {"frame": 1}, "invalid": {"frame": 10}, "later": {"frame": 20}},
                  "events": [{"kind": "text_input", "control": "field.ui_scale", "text": "3.5", "frame_after": 4},
                             {"kind": "control_click", "control": "preferences.apply", "frame_after": 8}]}
        required = [("field.ui_scale", "3.5"), ("preferences.apply", None)]
        reader.checkpoint_actions(report, "before", "invalid", required)
        with self.assertRaises(AssertionError):
            reader.checkpoint_actions(report, "invalid", "later", required)
        report["events"][0]["frame_after"] = 9
        report["events"].reverse()
        with self.assertRaises(AssertionError):
            reader.checkpoint_actions(report, "before", "invalid", required)

    def test_actual_pixel_center_coordinates_nonunit_framebuffer_and_all_surfaces(self):
        for name in ("ordinary", "XY", "XZ", "YZ"):
            for mode in ("nearest", "linear"):
                with self.subTest(name=name, mode=mode):
                    reader.check_gl_sample(sample(name, mode), mode)

    def test_sampler_state_must_match_pixels(self):
        for field, value in (("min_filter", 9728), ("mag_filter", 9728), ("wrap_s", 33071), ("width", 3)):
            row = sample()
            row["sampler"][field] = value
            with self.subTest(field=field), self.assertRaises(AssertionError):
                reader.check_gl_sample(row, "linear")

    def test_wrong_gl_y_origin_and_logical_center_are_refused(self):
        for field, value in (("pixel_xy_bottom_left", [100, 100]), ("pixel_center_logical", [90, 90])):
            row = sample()
            row["points"][0][field] = value
            with self.subTest(field=field), self.assertRaises(AssertionError):
                reader.check_gl_sample(row, "linear")

    def test_wrong_rgb_missing_surface_point_and_crosshair_displacement_are_refused(self):
        mutations = [lambda r: r["points"][0].update(rgb=[0, 0, 0]),
                     lambda r: r["points"][6].update(rgb=[0, 0, 0]),
                     lambda r: r["points"].pop(),
                     lambda r: r["points"][4].update(avoidance_offset_logical=[0, 0]),
                     lambda r: r["runtime"].update(renderer="")]
        for mutate in mutations:
            row = sample("XY")
            mutate(row)
            with self.subTest(mutation=mutate), self.assertRaises(AssertionError):
                reader.check_gl_sample(row, "linear")

    def test_background_cannot_be_declared_an_image_sample(self):
        row = sample()
        row["points"][6]["role"] = "image"
        with self.assertRaises(AssertionError):
            reader.check_gl_sample(row, "linear")

    def test_zero_viewport_nonfinite_scale_and_noninteger_pixel_are_refused(self):
        for mutate in (lambda r: r["runtime"].update(viewport=[0, 0, 0, 0]),
                       lambda r: r["runtime"].update(framebuffer_scale=[float("nan"), 1]),
                       lambda r: r["points"][0].update(pixel_xy_bottom_left=[True, 5])):
            row = sample()
            mutate(row)
            with self.assertRaises(AssertionError):
                reader.check_gl_sample(row, "linear")


class FullReportReader(unittest.TestCase):
    def submenu_hover_report(self):
        row = full_report()
        event = next(e for e in row["events"] if e["control"] == "host.menu.Image.Stacks")
        event.update(kind="control_hover", hovered_window="Image###Menu_00")
        opened = {"id": 987, "visible": True, "window_name": "Stacks###Menu_01"}
        event["opened_control"] = deepcopy(opened)
        for frame in row["frames"][event["frame_before"]+1:event["frame_after"]+1]:
            frame["raw_inputs"] = [{"kind": "position", "point": [30, 30]}]
        row["frames"][event["frame_after"]]["report"]["host_controls"] = {
            "menu.action.view.orthogonal": deepcopy(opened)}
        return row, event

    def test_actual_submenu_hover_with_observed_child_passes(self):
        row, _ = self.submenu_hover_report()
        reader.check_report(row)

    def test_submenu_hover_requires_position_no_buttons_and_observed_child(self):
        for mutation in ("button", "position", "hidden", "parent", "identity", "route"):
            row, event = self.submenu_hover_report()
            last = row["frames"][event["frame_after"]]
            if mutation == "button":
                last["raw_inputs"].append({"kind": "button", "button": 0, "down": False})
            elif mutation == "position":
                for frame in row["frames"][event["frame_before"]+1:event["frame_after"]+1]:
                    frame["raw_inputs"] = []
            elif mutation == "identity":
                event["opened_control"]["id"] += 1
            elif mutation == "route":
                event["control"] = "host.menu.file.open"
            else:
                field, value = ("visible", False) if mutation == "hidden" else ("window_name", event["hovered_window"])
                event["opened_control"][field] = value
                last["report"]["host_controls"]["menu.action.view.orthogonal"][field] = value
            with self.subTest(mutation=mutation), self.assertRaises(AssertionError):
                reader.check_report(row)

    def test_complete_literal_reports_and_fresh_pair_pass_in_both_contexts(self):
        for context in ("imgui_cpp", "imgui_python"):
            with self.subTest(context=context):
                pair = [json.loads(json.dumps(full_report(case, context), allow_nan=False))
                        for case in ("appearance-persist", "appearance-reopen")]
                self.assertEqual([len(row["states"]) for row in pair], [14, 8])
                for row in pair:
                    self.assertEqual(len(row["states"]["before"]["settings"]), 61)
                    self.assertLessEqual(len(row["frames"]), 750)
                    self.assertEqual(reader.check_report(row)["case_id"], row["case_id"])
                self.assertEqual(reader.check_pair(*pair), tuple(pair))

    def test_fractional_padding_before_imgui_style_truncation_is_refused(self):
        row = full_report()
        runtime = row["states"]["nearest_applied"]["observations"]["appearance_runtime"]
        # Vendored ImGuiStyle::ScaleAllSizes truncates each scaled component.
        self.assertEqual(runtime["frame_padding"], [10, 7])
        reader.check_report(row)
        runtime["frame_padding"] = [10.5, 7.5]
        with self.assertRaisesRegex(AssertionError, r"10\.5 != 10"):
            reader.check_report(row)

    def test_missing_required_state_is_refused(self):
        for case in ("appearance-persist", "appearance-reopen"):
            row = full_report(case)
            del row["states"]["new_owner"]
            with self.subTest(case=case), self.assertRaisesRegex(AssertionError, "complete fixed state"):
                reader.check_report(row)

    def test_invalid_drafts_require_the_specific_error_and_literal_draft(self):
        changes = [("invalid_scale", "error", "canvas_background: invalid color", "setting-specific"),
                   ("invalid_color", "error", "ui_scale: outside range", "setting-specific"),
                   ("invalid_scale", "ui_scale", 3., "invalid scale actual draft"),
                   ("invalid_color", "canvas_background", "#123456", "invalid color actual draft")]
        for name, field, value, message in changes:
            row = full_report()
            panel = row["states"][name]["observations"]["preferences_panel"]
            (panel if field == "error" else panel["draft"])[field] = value
            with self.subTest(name=name, field=field), self.assertRaisesRegex(AssertionError, message):
                reader.check_report(row)

    def test_owner_drop_or_replacement_cannot_establish_a_new_baseline(self):
        for replacement in (False, True):
            row = full_report()
            state = row["states"]["linked_sampled"]
            if replacement:
                state["owners"][1]["document_id"] = state["active_id"] = 99
            else:
                state["owners"].pop(0)
            with self.subTest(replacement=replacement), self.assertRaisesRegex(AssertionError, "stable exact owner membership"):
                reader.check_report(row)

    def test_sample_geometry_must_join_the_state_geometry(self):
        for name in ("ordinary_sampled", "linked_sampled"):
            row = full_report()
            row["states"][name]["gl_samples"][0]["geometry"]["image_rect"][0] += 1
            with self.subTest(name=name), self.assertRaisesRegex(AssertionError, "sampled geometry is the actual state geometry"):
                reader.check_report(row)

    def test_phase_map_and_unrelated_setting_changes_are_refused(self):
        for name, key, value, message in (("dpi_enabled_nearest", "interpolation", "linear", "exact phase map"),
                                           ("ordinary_sampled", "print_dpi", 300., "unrelated settings")):
            row = full_report()
            row["states"][name]["settings"][key] = value
            with self.subTest(name=name), self.assertRaisesRegex(AssertionError, message):
                reader.check_report(row)

    def test_refusal_checkpoint_cannot_borrow_a_later_apply(self):
        row = full_report()
        first = row["states"]["draft_reverted"]["frame"]
        last = row["states"]["invalid_scale"]["frame"]
        row["events"] = [e for e in row["events"] if not
                         (e["control"] == "preferences.apply" and first < e["frame_after"] <= last)]
        with self.assertRaisesRegex(AssertionError, "missing checkpoint action.*invalid_scale"):
            reader.check_report(row)

    def test_pair_requires_a_fresh_process_after_individual_reports_pass(self):
        first, second = full_report(), full_report("appearance-reopen")
        for runtime in (second["runtime_before"], second["runtime_after"], second["fixture"]["runtime"]):
            runtime["process_id"] = first["runtime_before"]["process_id"]
        reader.check_report(first)
        reader.check_report(second)
        with self.assertRaisesRegex(AssertionError, "fresh process reopen"):
            reader.check_pair(first, second)

    def test_pair_requires_exact_saved_bytes_even_when_rehashed_valid_json(self):
        first, second = full_report(), full_report("appearance-reopen")
        for state in second["states"].values():
            file = state["preferences_file"]
            file["utf8"] += "\n"
            raw = file["utf8"].encode("utf-8")
            file.update(byte_count=len(raw), sha256=hashlib.sha256(raw).hexdigest())
        reader.check_report(first)
        reader.check_report(second)
        with self.assertRaisesRegex(AssertionError, "same byte-exact actual saved preferences"):
            reader.check_pair(first, second)


if __name__ == "__main__":
    unittest.main()
