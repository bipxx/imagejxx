"""Synthetic C3 protocol controls. No product, GUI, compiler or capture runs.

The fixture describes the wire and literal inputs independently of the reader.
Only the separately owned numerical oracle supplies expected pixel words and
the unchanged R011 curve. Passing these controls is not runtime qualification.
"""
from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True


def source_module(name):
    path = ROOT / "tests/python" / (name + ".py")
    raw = path.read_bytes()
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    exec(compile(raw, str(path), "exec", dont_inherit=True), module.__dict__)
    if path.read_bytes() != raw:
        raise ValueError("C3 control source changed while loading")
    return module


source_module("actual_observation_transport")
oracle = source_module("settings_drawing_expectations")
reader = source_module("settings_drawing_reader")

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
FINAL = {"foreground": "#e01f40", "background": "#1020e0", "brush_width": 3.,
         "rounded_corner_diameter": 5, "line_width": 3., "point_size": 5., "text_size": 23}
THIN = dict(FINAL, brush_width=1., line_width=1., point_size=1.)
SEED = {"format": "imagejxx-preferences", "version": 1,
        "values": {"c3_future_value": {"raw": [1, "two", True]}},
        "c3_future_root": {"opaque": ["retain", 17]}, "extras": {"c3": "retain extra"}}
PHASES = ("before", "after", "undo", "redo", "restored")
POINTS = {"rectangle": ([2, 2], [5, 5]), "rounded_rectangle": ([9, 9], [19.5, 16.5]),
          "line": ([2, 4], [6, 4]), "point": ([4, 4], None), "text": ([1, 1], None),
          "brush": ([4, 4], None), "eraser": ([4, 4], None)}


def selection(kind, points, width=1):
    return {"kind": kind, "points": points, "width": width}


def pixels(values):
    return {"shape": [33, 41, 3], "hex": bytes(values).hex(),
            "storage_f64le_hex": struct.pack("<" + "d" * len(values), *values).hex()}


def mask_observation(mask, curve=None):
    return {"mask_shape": [33, 41], "mask_u8_hex": bytes(255 * bit for bit in mask).hex(), "rounded_path": curve}


def payload(settings):
    result = deepcopy(SEED)
    result["values"].update(settings)
    return result


def file_observation(value):
    raw = json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n"
    return {"path": "/synthetic/c3/preferences.json", "exists": True, "utf8": raw,
            "byte_count": len(raw.encode()), "sha256": hashlib.sha256(raw.encode()).hexdigest()}


def source_owner(identity):
    return {"document_id": identity, "data_identity": 0, "scientific": False,
        "shape": [1, 1, 1, 33, 41, 3], "dtype": "uint8", "sample_policy": "finite_only",
        "pixels": pixels(oracle.initial_rgb()), "selection": None, "selection_observation": None,
        "previous": selection("line", [[30, 28], [38, 28]]),
        "previous_observation": mask_observation([int(y == 28 and 30 <= x <= 38) for y in range(33) for x in range(41)]),
        "metadata": {"title": "C3 drawing", "properties": {"c3": {"label": "retain properties", "value": 17}},
                     "extensions": {"c3": "retain extension"}, "record_layout": {"c3": "retain layout"}},
        "calibration": {"spacing": [1, 1, 1], "origin": [0, 0, 0], "direction": [1, 0, 0, 0, 1, 0, 0, 0, 1],
                        "unit": "px", "frame_interval": 1, "time_origin": 0, "time_unit": "s"},
        "rois": [{"name": "C3 retained ROI", "selection": selection("rectangle", [[36, 28], [39, 31]]),
                  "t": None, "c": None, "z": None, "attributes": {"c3": "retain ROI"}}],
        "overlays": [{"kind": "point", "points": [[39., 31.]], "width": 1, "c3": "retain overlay"}],
        "display": {"t": 0, "c": 0, "crosshair": [20, 16, 0], "orthogonal": False, "linked_zoom": True,
                    "zoom": 8, "plane_zoom": [1, 1, 1], "pan": [[0, 0]] * 3, "revision": 1,
                    "properties": {"display_range": [0, 255]}},
        "revisions": {"pixels": 0, "metadata": 0, "selection": 0, "display": 1},
        "dirty": False, "undo": 0, "redo": 0,
        "history": {"undo_steps": 0, "redo_steps": 0, "step_limit": 8, "pixel_budget": 0,
                    "pixels": {"logical_bytes": 32472, "current_bytes": 32472, "retained_bytes": 0,
                               "obsolete_bytes": 0, "retained_planes": 0, "exact": True}}}


class WireFixture:
    """A synthetic event/state model; it never invokes the production producer."""
    def __init__(self, case="drawing-persist", context="imgui_cpp"):
        self.persist = case == "drawing-persist"
        self.settings = deepcopy(DEFAULT_SETTINGS)
        if not self.persist:
            self.settings.update(FINAL)
        self.draft = deepcopy(self.settings)
        self.file = file_observation(SEED if self.persist else payload(self.settings))
        self.owner = source_owner(11 if self.persist else 21)
        self.next_identity = 0
        self.panel_open = False
        self.dialog = {"open": False, "controls": {}}
        self.geometry = {"open": False, "corner_diameter": 5, "control_states": {}}
        self.ids = {}
        self.canvas = {"visible": True, "canvas_id": 700, "origin": [100, 100], "scale": [8, 8]}
        runtime = {"process_id": 101 if self.persist else 102, "parent_process_id": 99,
                   "pointer_bits": 64, "producer_source": "/synthetic/source/probe.cpp", "imgui": "SYNTHETIC"}
        for role in ("executable", "producer_artifact", "native_artifact"):
            runtime[role] = {"path": "/synthetic/" + context + "/" + role, "bytes": 9,
                             "sha256": hashlib.sha256(b"SYNTHETIC").hexdigest()}
        self.report = {"schema": "c3.settings-drawing.ui.001", "case_id": case, "context": context,
            "state": "observed", "acceptance_claim": False, "error": "", "synthetic_protocol_only": True,
            "runtime_before": runtime, "runtime_after": deepcopy(runtime), "elapsed_seconds": 5.,
            "fixture": {"shape": [1, 1, 1, 33, 41, 3], "dtype": "uint8", "sample_formula": "RGB=[10+x,20+y,30+x+y]",
                        "runtime": {"process_id": runtime["process_id"], "parent_process_id": 99},
                        "producer_source": runtime["producer_source"], "imgui": runtime["imgui"],
                        "font_scope": "Text dialog/default and metadata only; no font or glyph parity claim"},
            "bounds": {"frames": 750, "seconds": 60, "report_bytes_exclusive": 16 * 1024**2, "preference_bytes": 1024**2},
            "states": {}, "events": [], "frames": [], "setup_events": []}
        self.report["setup_events"].append({"kind": "fixture.source", "settings_mutation": False, "owner": deepcopy(self.owner)})
        self.frame()

    def panel(self):
        fields = {}
        for name in FINAL:
            kind = "color" if name in ("foreground", "background") else "integer" if name in ("text_size", "rounded_corner_diameter") else "number"
            field = dict(self.control("field." + name), kind=kind, draft=self.draft[name], applied=self.settings[name])
            if kind == "color":
                field["editor"] = self.control("field." + name)
            fields[name] = field
        return {"open": self.panel_open, "group": "Drawing", "error": "", "fields": fields}

    def control(self, name):
        identifier = self.ids.setdefault(name, len(self.ids) + 100)
        return {"id": identifier, "point": [30, 30], "rect": [20, 20, 40, 40], "clip_rect": [0, 0, 500, 500],
                "window_name": "Synthetic controls", "enabled": True, "disabled": False, "visible": True}

    def frame(self, atoms=(), *, item=None, focus=0, mouse=None):
        observed = {"input_frame": {"raw_inputs": deepcopy(list(atoms)), "app_focus_lost": False,
                        "mouse_position": mouse if mouse is not None else [30, 30], "focus": {"active_id": focus},
                        "input_text": {"id": focus}},
                    "window_error": "", "error_modal": False, "ordinary_canvas": deepcopy(self.canvas),
                    "preferences_panel": self.panel(), "dialog": deepcopy(self.dialog), "selection_properties": deepcopy(self.geometry),
                    "actual_hover": {"id": item["id"] if item else 0, "window": item["window_name"] if item else ""}}
        self.report["frames"].append({"ordinal": len(self.report["frames"]), "label": "synthetic",
                                      "events": deepcopy(list(atoms)), "report": observed})

    def ordinal(self):
        return len(self.report["frames"]) - 1

    def click(self, name):
        start, item = self.ordinal(), self.control(name)
        self.frame([{"type": "position", "point": [30, 30]}], item=item)
        self.frame([{"type": "button", "name": "left", "down": True}], item=item)
        self.frame([{"type": "button", "name": "left", "down": False}], item=item)
        self.report["events"].append({"kind": "control_click", "control": name, "actual_control": item,
            "actual_mouse": [30, 30], "actual_clip": [0, 0, 500, 500], "hovered_id": item["id"],
            "hovered_window": item["window_name"], "frame_before": start, "frame_after": self.ordinal()})

    def text(self, name, value):
        start = self.ordinal()
        self.click(name)
        item = self.control(name)
        if name.startswith("dialog."):
            self.dialog["controls"][name[7:]] = item
        if name.startswith("geometry."):
            self.geometry["control_states"][name[9:]] = item
        for down in (True, False):
            self.frame([{"type": "keys", "keys": [{"name": "Ctrl", "down": down}, {"name": "A", "down": down}]}], focus=item["id"])
        self.frame([{"type": "text", "value": value}], focus=item["id"])
        for down in (True, False):
            self.frame([{"type": "key", "name": "Enter", "down": down}], focus=item["id"])
        self.report["events"].append({"kind": "text_input", "control": name, "text": value, "input_id": item["id"],
                                      "frame_before": start, "frame_after": self.ordinal()})

    def menu(self, item, selection_menu=False):
        self.click("host.menu.Edit")
        if selection_menu:
            self.click("host.menu.Edit.Selection")
        self.click("host." + item)

    def preferences(self):
        self.click("host.menu.Edit"); self.click("host.menu.Edit.Preferences")
        self.panel_open = True; self.draft = deepcopy(self.settings)
        self.click("preferences.group"); self.click("group.Drawing")

    def apply(self):
        self.click("preferences.apply")
        self.settings = deepcopy(self.draft)
        self.file = file_observation(payload(self.settings))

    def close(self):
        self.click("preferences.close_button"); self.panel_open = False

    def save(self, name, alias=None):
        if alias:
            self.report["states"][name] = deepcopy(self.report["states"][alias]); return
        self.frame()
        self.report["states"][name] = {"frame": self.ordinal(), "active_id": self.owner["document_id"],
            "settings": deepcopy(self.settings), "preferences": payload(self.settings), "preferences_file": deepcopy(self.file),
            "owners": [deepcopy(self.owner)], "workflow": [], "results": [], "recording": False, "error": "",
            "usage": {"running": 0, "queued": 0, "running_bytes": 0, "retained_bytes": 0, "reserved_bytes": 0},
            "observations": {"preferences_panel": self.panel(), "dialog": deepcopy(self.dialog),
                             "selection_properties": deepcopy(self.geometry), "tools": {}, "ordinary_pixels": {}}}

    def select(self, chosen, observed):
        self.owner["previous"] = deepcopy(self.owner["selection"])
        self.owner["previous_observation"] = deepcopy(self.owner["selection_observation"])
        self.owner["selection"], self.owner["selection_observation"] = chosen, observed
        self.owner["revisions"]["selection"] += 1

    def deselect(self):
        if self.owner["selection"] is not None:
            self.menu("menu.action.select_none", True)
            self.select(None, None)

    def pointer(self, label, point):
        start = self.ordinal()
        screen = [100 + p*8 for p in point]
        self.frame([{"type": "position", "point": screen}], mouse=screen)
        self.report["events"].append({"kind": "canvas_position", "control": label, "point": point, "screen": screen,
            "actual_image_point": point[:], "canvas": deepcopy(self.canvas), "hovered_id": 700,
            "hovered_window": "Images", "frame_before": start, "frame_after": self.ordinal()})

    def gesture(self, tool):
        self.click("host.tools.tool." + tool)
        start, end = POINTS[tool]
        self.pointer(tool + ".position", start[:])
        self.frame([{"type": "button", "name": "left", "down": True}])
        if end is not None:
            self.pointer(tool + ".drag", end[:])
        self.frame([{"type": "button", "name": "left", "down": False}])
        width = self.settings["line_width"]
        if tool == "rectangle":
            self.select(selection(tool, [[2, 2], [5, 5]], width), mask_observation(oracle.expected_mask("fill")))
        elif tool == "rounded_rectangle":
            # The interim curve is opaque in this protocol fixture. Only the
            # eventual R011 is an independently specified numerical witness.
            self.select(dict(selection(tool, [[9, 9], [20, 17]], width), corner_diameter=5), mask_observation([0]*1353, [0]*44))
        elif tool in ("line", "point"):
            if tool == "point":
                width = self.settings["point_size"]
            name = ("thin_" if width == 1 else "") + tool
            self.select(selection(tool, [start] + ([end] if end else []), width), mask_observation(oracle.expected_mask(name)))
        elif tool == "text":
            self.dialog = {"open": True, "title": "Text annotation", "values": {"text": "Label", "font_size": 23,
                           "color": "#e01f40", "x": 1, "y": 1}, "text": {"text": "Label"}, "controls": {}}

    def cycle(self, name, action, *, alias=None):
        self.save(name + "_before", alias)
        before = deepcopy(self.owner)
        action()
        self.next_identity += 1
        edited = deepcopy(self.owner)
        edited["pixels"] = pixels(oracle.expected_rgb(name))
        if name == "text":
            edited["overlays"].append({"kind": "text", "points": [[1, 1]], "width": 1,
                                      "text": "C3", "color": "#e01f40", "font_size": 23, "slice_index": 0})
            self.dialog["open"] = False
        changed = "metadata" if name == "text" else "pixels"
        for offset, phase in enumerate(("after", "undo", "redo", "restored"), 1):
            if phase != "after":
                self.menu("menu.Edit." + ("Redo" if phase == "redo" else "Undo"))
            active = phase in ("after", "redo")
            self.owner = deepcopy(edited if active else before)
            self.owner["revisions"][changed] = before["revisions"][changed] + offset
            self.owner["data_identity"] = self.next_identity if active else before["data_identity"]
            self.owner["dirty"] = active or before["dirty"]
            self.owner["undo"], self.owner["redo"] = (before["undo"] + 1, 0) if active else (before["undo"], 1)
            self.owner["history"].update(undo_steps=self.owner["undo"], redo_steps=self.owner["redo"])
            self.save(name + "_" + phase)

    def run(self):
        self.save("initial")
        initial = self.report["states"]["initial"]
        self.report["setup_events"].append(dict(kind="fixture.preferences_input", **{
            k: deepcopy(initial[k]) for k in ("preferences_file", "preferences", "settings")}))
        self.preferences(); self.save("preferences_shown")
        if self.persist:
            for key, value in (("foreground", "#e01f40"), ("background", "#1020e0"), ("brush_width", "1"),
                               ("rounded_corner_diameter", "5"), ("line_width", "1"), ("point_size", "1"), ("text_size", "23")):
                self.text("field."+key, value); self.draft[key] = THIN[key]
            self.save("thin_draft"); self.apply(); self.save("thin_applied"); self.close(); self.save("thin_closed")
            self.click("host.tools.tool.brush")
            self.cycle("thin_brush", lambda: self.gesture("brush"))
            for tool in ("line", "point"):
                self.gesture(tool); self.cycle("thin_"+tool, lambda: self.menu("menu.action.fill"))
            self.preferences()
            for key, value in (("brush_width", "3"), ("line_width", "3"), ("point_size", "5")):
                self.text("field."+key, value); self.draft[key] = FINAL[key]
            self.save("final_draft"); self.apply(); self.save("final_applied"); self.close(); self.save("final_closed")
        else:
            self.close(); self.save("reopened_closed")
        self.gesture("rectangle")
        self.cycle("fill", lambda: self.menu("menu.action.fill"))
        self.cycle("clear", lambda: self.menu("menu.action.clear"), alias="fill_restored")
        self.deselect(); self.click("host.tools.tool.brush")
        self.cycle("brush", lambda: self.gesture("brush"))
        self.click("host.tools.tool.eraser"); self.cycle("eraser", lambda: self.gesture("eraser"))
        self.gesture("rounded_rectangle"); self.save("rounded_constructed")
        self.menu("menu.action.show_selection_properties", True); self.geometry["open"] = True
        self.save("rounded_geometry_shown")
        for key, value in (("Center X", "14.25"), ("Center Y", "12.75"), ("Width", "10.5"), ("Height", "7.5"), ("Angle (degrees)", "23")):
            self.text("geometry."+key, value)
        self.click("geometry.Apply"); self.geometry["open"] = False
        reference = oracle.rounded_reference()
        x, y, ux, uy, vx, vy = reference["frame"]
        self.select(dict(selection("rotated_rounded_rectangle", [[x, y], [x+ux, y+uy], [x+vx, y+vy]], 3), corner_diameter=5),
                    mask_observation(reference["mask"], list(reference["curve"])))
        self.cycle("rounded", lambda: self.menu("menu.action.fill"))
        for tool in ("line", "point"):
            self.gesture(tool); self.cycle(tool, lambda: self.menu("menu.action.fill"))
        self.deselect(); self.gesture("text"); self.save("text_opened")
        self.text("dialog.text", "C3"); self.dialog["text"]["text"] = "C3"
        self.cycle("text", lambda: self.click("dialog.apply"))
        self.save("final", "text_restored")
        self.report["setup_events"].append({"kind": "session.closed", "lifecycle": {"open": False, "context_id": 0, "current_context_id": 0}})
        return self.report


def report(case="drawing-persist", context="imgui_cpp"):
    return WireFixture(case, context).run()


class DrawingReaderControls(unittest.TestCase):
    def refusal(self, mutate, *, case="drawing-persist"):
        value = report(case)
        mutate(value)
        with self.assertRaises((AssertionError, ValueError, KeyError, TypeError)):
            reader.check_report(value)

    def test_exact_four_report_routes_and_pair(self):
        for context in ("imgui_cpp", "imgui_python"):
            with self.subTest(context=context):
                a, b = report(context=context), report("drawing-reopen", context)
                self.assertEqual(len(a["states"]), 67)
                self.assertEqual(len(b["states"]), 47)
                self.assertIs(reader.check_report(a, context, "drawing-persist"), a)
                self.assertIs(reader.check_report(b, context, "drawing-reopen"), b)
                self.assertIsNone(reader.check_pair(a, b))

    def test_transport_uses_original_decoder_and_rejects_corrupted_blob(self):
        value = report()
        raw = json.dumps(value["states"]["brush_after"]["owners"][0]["pixels"], separators=(",", ":"))
        key = hashlib.sha256(raw.encode()).hexdigest()
        value["states"]["brush_after"]["owners"][0]["pixels"] = {"$actual_blob": key}
        value["actual_blob_transport"] = {"version": 1, "encoding": "utf8-json", "blobs": {key: raw}}
        decoded = reader.check_report(value)
        self.assertEqual(decoded["states"]["brush_after"]["owners"][0]["pixels"], pixels(oracle.expected_rgb("brush")))
        value["actual_blob_transport"]["blobs"][key] += " "
        with self.assertRaises(AssertionError):
            reader.check_report(value)

    def test_outside_declared_u8_word_refused(self):
        self.refusal(lambda r: r["states"]["fill_after"]["owners"][0]["pixels"].update(hex="00" + r["states"]["fill_after"]["owners"][0]["pixels"]["hex"][2:]))

    def test_backing_float_word_independent_of_uint8_refused(self):
        self.refusal(lambda r: r["states"]["clear_after"]["owners"][0]["pixels"].update(storage_f64le_hex="0000000000000000" + r["states"]["clear_after"]["owners"][0]["pixels"]["storage_f64le_hex"][16:]))

    def test_thin_brush_cannot_substitute_wide_result(self):
        self.refusal(lambda r: r["states"]["thin_brush_after"]["owners"][0].update(pixels=pixels(oracle.expected_rgb("brush"))))

    def test_foreground_and_background_are_distinct_consumers(self):
        for name in ("clear", "eraser"):
            with self.subTest(name=name):
                self.refusal(lambda r: r["states"][name+"_after"]["owners"][0].update(pixels=pixels(oracle.expected_rgb("fill" if name == "clear" else "brush"))))

    def test_all_native_rounded_curve_coefficients_checked(self):
        self.refusal(lambda r: r["states"]["rounded_before"]["owners"][0]["selection_observation"]["rounded_path"].__setitem__(43, 999.))

    def test_complete_rounded_mask_checked(self):
        self.refusal(lambda r: r["states"]["rounded_after"]["owners"][0]["selection_observation"].update(mask_u8_hex="ff" + r["states"]["rounded_after"]["owners"][0]["selection_observation"]["mask_u8_hex"][2:]))

    def test_rounded_kind_diameter_and_rotation_cannot_be_substituted(self):
        for key, value in (("kind", "rounded_rectangle"), ("corner_diameter", 6), ("width", 1)):
            with self.subTest(key=key):
                self.refusal(lambda r: r["states"]["rounded_before"]["owners"][0]["selection"].update({key: value}))

    def test_line_and_point_capture_width(self):
        for name in ("thin_line", "thin_point", "line", "point"):
            with self.subTest(name=name):
                self.refusal(lambda r: r["states"][name+"_before"]["owners"][0]["selection"].update(width=2))

    def test_text_metadata_is_checked_without_font_claim(self):
        for key, value in (("font_size", 16), ("color", "#ffffff"), ("text", "wrong"), ("slice_index", None)):
            with self.subTest(key=key):
                self.refusal(lambda r: r["states"]["text_after"]["owners"][0]["overlays"][-1].update({key: value}))

    def test_text_pixels_remain_exact(self):
        self.refusal(lambda r: r["states"]["text_after"]["owners"][0].update(pixels=pixels(oracle.expected_rgb("point"))))

    def test_text_draft_buffer_is_authoritative_before_apply(self):
        value = report()
        draft = value["states"]["text_before"]["observations"]["dialog"]
        self.assertEqual(draft["values"]["text"], "Label")
        self.assertEqual(draft["text"]["text"], "C3")
        reader.check_report(value)
        # An empty defaults slot is likewise not the edited InputText buffer.
        draft["values"]["text"] = ""
        reader.check_report(value)
        # This would satisfy the old values-based predicate while lying about
        # the actual text that Apply parses. Overlay expectations stay intact.
        draft["values"]["text"] = "C3"
        draft["text"]["text"] = "wrong"
        with self.assertRaisesRegex(AssertionError, "actual Text draft/Apply lifecycle"):
            reader.check_report(value)

    def test_undo_redo_identity_and_counts(self):
        for phase, key, value in (("undo", "data_identity", 900), ("redo", "undo", 0), ("restored", "redo", 0)):
            with self.subTest(phase=phase):
                self.refusal(lambda r: r["states"]["fill_"+phase]["owners"][0].update({key: value}))

    def test_nonedit_selection_transition_cannot_publish_image_history(self):
        self.refusal(lambda r: r["states"]["point_before"]["owners"][0].update(data_identity=44))

    def test_previous_selection_is_preserved_and_rebound(self):
        self.refusal(lambda r: r["states"]["brush_before"]["owners"][0].update(previous=None, previous_observation=None))

    def test_metadata_carriers_and_calibration_preserved(self):
        for key in ("metadata", "calibration", "rois", "display"):
            with self.subTest(key=key):
                self.refusal(lambda r: r["states"]["line_restored"]["owners"][0].update({key: {}}))

    def test_unrelated_setting_and_each_unknown_compartment_preserved(self):
        self.refusal(lambda r: r["states"]["final_closed"]["settings"].update(theme="light"))
        for key in ("values", "extras", "c3_future_root"):
            with self.subTest(key=key):
                self.refusal(lambda r: r["states"]["thin_applied"]["preferences"].update({key: {}}))

    def test_original_preference_bytes_are_hashed(self):
        self.refusal(lambda r: r["states"]["initial"]["preferences_file"].update(utf8=r["states"]["initial"]["preferences_file"]["utf8"]+" "))

    def test_known_empty_recent_default_does_not_replace_unknown_extras(self):
        value = report()
        for state in value["states"].values():
            state["preferences"]["extras"]["recent"] = []
        value["setup_events"][1]["preferences"] = deepcopy(value["states"]["initial"]["preferences"])
        reader.check_report(value)
        value["states"]["thin_applied"]["preferences"]["extras"].pop("c3")
        with self.assertRaises(AssertionError):
            reader.check_report(value)

    def test_recent_files_cannot_be_populated_or_malformed(self):
        for recent in (["unexpected.tif"], {}, None, "", False):
            with self.subTest(recent=recent):
                value = report()
                value["states"]["thin_applied"]["preferences"]["extras"]["recent"] = recent
                with self.assertRaisesRegex(AssertionError, "recent files"):
                    reader.check_report(value)

    def test_draft_cannot_appear_applied_before_actual_apply(self):
        self.refusal(lambda r: r["states"]["thin_draft"]["settings"].update(brush_width=1.))

    def test_exact_membership_and_only_two_same_frame_aliases(self):
        self.refusal(lambda r: r["states"].pop("rounded_geometry_shown"))
        self.refusal(lambda r: r["states"]["point_before"].update(frame=r["states"]["line_restored"]["frame"]))
        self.refusal(lambda r: r["states"]["clear_before"].update(error="alias changed"))

    def test_actual_control_hover_and_clipping(self):
        self.refusal(lambda r: r["events"][0].update(hovered_id=0))
        self.refusal(lambda r: r["events"][0].update(actual_clip=[0, 0, 25, 25]))

    def test_missing_actual_input_cannot_be_replaced_by_requested_event(self):
        def mutate(r):
            event = next(e for e in r["events"] if e["kind"] == "text_input")
            delivered = next(f for f in r["frames"][event["frame_before"]:event["frame_after"]+1]
                             if {"type": "text", "value": event["text"]} in f["events"])
            delivered["report"]["input_frame"]["raw_inputs"] = []
        self.refusal(mutate)

    def test_real_field_focus_and_enter_release_required(self):
        def mutate(r, focus):
            event = next(e for e in r["events"] if e["kind"] == "text_input")
            interval = r["frames"][event["frame_before"]+1:event["frame_after"]+1]
            if focus:
                frame = next(f for f in interval if any(a.get("type") == "text" for a in f["events"]))
                frame["report"]["input_frame"]["focus"]["active_id"] = 0
            else:
                frame = interval[-1]; frame["events"] = []; frame["report"]["input_frame"]["raw_inputs"] = []
        for focus in (False, True):
            with self.subTest(focus=focus):
                self.refusal(lambda r: mutate(r, focus))

    def test_canvas_uses_real_transform_and_press_release(self):
        self.refusal(lambda r: next(e for e in r["events"] if e["kind"] == "canvas_position")["actual_image_point"].__setitem__(0, 4.5))
        def erase_press(r):
            event = next(e for e in r["events"] if e["kind"] == "canvas_position")
            frame = r["frames"][event["frame_after"]+1]
            frame["events"] = []; frame["report"]["input_frame"]["raw_inputs"] = []
        self.refusal(erase_press)

    def test_exact_geometry_and_text_recipe_required(self):
        for control in ("geometry.Angle (degrees)", "dialog.text"):
            with self.subTest(control=control):
                self.refusal(lambda r: next(e for e in r["events"] if e["kind"] == "text_input" and e["control"] == control).update(text="wrong"))

    def test_reopen_cannot_reapply_preferences(self):
        self.refusal(lambda r: r["events"][0].update(control="preferences.apply"), case="drawing-reopen")

    def test_pair_fresh_process_original_path_and_bytes(self):
        for mutation in ("pid", "context", "path"):
            with self.subTest(mutation=mutation):
                a, b = report(), report("drawing-reopen")
                if mutation == "pid":
                    for name in ("runtime_before", "runtime_after"):
                        b[name]["process_id"] = 101
                    b["fixture"]["runtime"]["process_id"] = 101
                elif mutation == "context":
                    b["context"] = "imgui_python"
                else:
                    for state in b["states"].values():
                        state["preferences_file"]["path"] = "/synthetic/other/preferences.json"
                    b["setup_events"][1]["preferences_file"]["path"] = "/synthetic/other/preferences.json"
                with self.assertRaises(AssertionError):
                    reader.check_pair(a, b)

    def test_observation_never_promotes_runtime_or_changes_bounds(self):
        self.refusal(lambda r: r.update(acceptance_claim=True))
        self.refusal(lambda r: r.update(elapsed_seconds=60))
        self.refusal(lambda r: r["bounds"].update(frames=751))
        self.refusal(lambda r: r["runtime_after"]["executable"].update(sha256="0"*64))


if __name__ == "__main__":
    unittest.main()
