"""Capture existing C++ UI probes for independent Python test readers.

Each process gets private preferences and a fresh capture directory. Reports are
copied byte for byte into the readers' directory layout; originals stay alive
because some reports contain absolute artifact paths. Captures are observations,
not acceptance verdicts. The Python readers supply the independent assertions.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import time
import zipfile


def pin(path):
    path = Path(path)
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return {"bytes": path.stat().st_size, "sha256": digest.hexdigest()}


def planar_cases():
    """Named cases for fresh C++ input, independently checked by Python readers."""
    operations = ["rectangle", "ellipse", "line", "point", "freehand", "polygon",
                  "multipoint", "escape", "focus", "context", "source", "wheel",
                  "zoom", "preferences", "resize", "vertex", "rotate", "mask_move", "mask_hole",
                  "busy", "scientific_busy", "modal", "time", "channel", "owner", "style", "mask_rotate"]
    modifiers = ("none", "shift", "alt", "both")
    for gesture in ("resize", "cross"):
        operations.extend(f"{gesture}_{handle}_{modifier}"
                          for handle in range(8) for modifier in modifiers)
    operations.extend("collapse_" + modifier for modifier in modifiers)
    operations.extend("invalid_" + name for name in (
        "width_zero", "width_negative", "width_nan", "width_inf", "width_overflow",
        "width_partial", "height_zero", "center_nan", "angle_inf", "fixed_negative",
        "time_zero", "channel_over"))
    operations.extend(f"profile_{profile}_cross_{handle}_{modifier}"
                      for profile in ("ellipse", "rbox", "rellipse")
                      for handle in range(8) for modifier in modifiers)
    operations.extend("profile_ellipse_collapse_" + modifier for modifier in modifiers)
    return [f"{plane}.{operation}" for plane in ("XY", "XZ", "YZ") for operation in operations]


def bigtiff_cases():
    """Committed finite edge fixtures used by the independent BigTIFF reader."""
    directory = Path(__file__).resolve().parents[1] / "tests/fixtures/scientific-bigtiff"
    manifest = json.loads((directory / "SOURCE.json").read_bytes())
    for row in manifest["files"]:
        if row["valid"] and "-edges-" in row["file"] and row["pattern"] == "finite":
            if Path(row["file"]).name != row["file"]:
                raise ValueError("BigTIFF fixture must be a direct manifest member")
            yield Path(row["file"]).stem, ["--scientific-tiff-ui-case", row["file"]]


# Environment name and whether readers expect an extra capture001 directory.
TIFF_READERS = {
    "uint8-tiff": ("IMAGEJXX_UINT8_TIFF_CPP_DIR", True),
    "rgb-tiff": ("IMAGEJXX_RGB_TIFF_CPP_DIR", True),
    "deflate-tiff": ("IMAGEJXX_DEFLATE_TIFF_CPP_DIR", True),
    "packbits-tiff": ("IMAGEJXX_PACKBITS_TIFF_CPP_DIR", True),
    "lzw-tiff": ("IMAGEJXX_LZW_TIFF_CPP_DIR", True),
    "horizontal-tiff": ("IMAGEJXX_HORIZONTAL_TIFF_CPP_DIR", True),
    "float32-tiff": ("IMAGEJXX_FLOAT32_TIFF_CPP_DIR", False),
    "deflate-float32": ("IMAGEJXX_DEFLATE_FLOAT32_CPP_DIR", False),
    "packbits-float32": ("IMAGEJXX_PACKBITS_FLOAT32_CPP_DIR", False),
    "lzw-float32": ("IMAGEJXX_LZW_FLOAT32_CPP_DIR", False),
    "floating-tiff": ("IMAGEJXX_FLOATING_TIFF_CPP_DIR", False),
    "tiled-tiff": ("IMAGEJXX_TILED_TIFF_CPP_DIR", True),
    "tiled-layouts": ("IMAGEJXX_TILED_LAYOUTS_CPP_DIR", True),
    "deflate-tiles": ("IMAGEJXX_DEFLATE_TILES_CPP_DIR", True),
    "deflate-tiles-float": ("IMAGEJXX_DEFLATE_TILES_FLOAT_CPP_DIR", True),
    "packbits-lzw-tiles": ("IMAGEJXX_PACKBITS_LZW_TILES_CPP_DIR", True),
    "packbits-lzw-tiles-float": ("IMAGEJXX_PACKBITS_LZW_TILES_FLOAT_CPP_DIR", True),
    "predicted-tiles": ("IMAGEJXX_PREDICTED_TILES_CPP_DIR", True),
    "predicted-tiles-float": ("IMAGEJXX_PREDICTED_TILES_FLOAT_CPP_DIR", True),
}


# These existing readers consume path-only indices, never inline UI results.
INDEXED_READERS = {
    "orientation-draft": ("--scientific-orientation-ui-case", "IMAGEJXX_ORIENTATION_DRAFT_CPP_REPORT",
                          "p06i.scientific-orientation-ui.001", ("O06",)),
    "scientific-targets": ("--scientific-operation-targets-ui-case", "IMAGEJXX_SCIENTIFIC_TARGETS_CPP_REPORT",
                           "p06d1.scientific-operation-targets-ui.001",
                           ("D01", "D02", "D03", "D03M", "D03E", "D04", "D04X", "D05", "D05T", "D06", "D07")),
    "scientific-slicing": ("--scientific-slicing-ui-case", "IMAGEJXX_SCIENTIFIC_SLICING_CPP_REPORT",
                           "p06e.scientific-slicing-ui.001", tuple(f"E{i:02d}" for i in range(1, 11))),
    "auto-contrast": ("--auto-contrast-ui-case", "IMAGEJXX_AUTO_CONTRAST_CPP_REPORT",
                      "p06c4.auto-contrast-ui.001", tuple(f"U{i:02d}" for i in range(1, 9))),
    "channel-display": ("--channel-display-ui-case", "IMAGEJXX_CHANNEL_DISPLAY_CPP_REPORT",
                        "p06c.channel-display-ui.001",
                        ("C01A", "C01B", "C01C", "C02A", "C02B", "C03A", "C03B",
                         "C04A", "C04B", "C04C", "C05A", "C05B", "C05C",
                         "C06A", "C06B", "C06C", "C06D", "C06E")),
    "channel-max": ("--channel-display-ui-case", "IMAGEJXX_CHANNEL_DISPLAY_PROJECTION_CPP_REPORT",
                    "p06c2.channel-display-projection-ui.001", tuple(f"M{i:02d}" for i in range(1, 11))),
    "channel-min": ("--channel-display-ui-case", "IMAGEJXX_CHANNEL_DISPLAY_MIN_CPP_REPORT",
                    "p06c3.channel-display-min-ui.001", tuple(f"N{i:02d}" for i in range(1, 11))),
}


# Existing one-report editors; every report is copied without rewriting paths.
EDITOR_READERS = {
    "annotation-workflow": ("--annotation-workflow-ui-case", "IMAGEJXX_ANNOTATION_WORKFLOW_CPP_REPORT"),
    "planar-path": ("--planar-path-ui-case", "IMAGEJXX_PLANAR_PATH_CPP_REPORT"),
    "native-bare-import": ("--native-bare-import-ui-case", "IMAGEJXX_NATIVE_BARE_CPP_REPORT"),
    "rgb-dataset": ("--rgb-dataset-ui-case", "IMAGEJXX_RGB_CPP_REPORT"),
    "scientific-process": ("--scientific-process-ui-case", "IMAGEJXX_SCIENTIFIC_PROCESS_CPP_REPORT"),
    "bare-edit": ("--bare-edit-ui-case", "IMAGEJXX_BARE_EDIT_CPP_REPORT"),
    "mixed-import": ("--mixed-import-ui-case", "IMAGEJXX_MIXED_IMPORT_CPP_REPORT"),
    "roi-import": ("--roi-import-ui-case", "IMAGEJXX_ROI_IMPORT_CPP_REPORT"),
    "display-annotation": ("--display-annotation-ui-case", "IMAGEJXX_DISPLAY_CPP_REPORT"),
    "roi-zip": ("--roi-zip-export-ui-case", "IMAGEJXX_ROI_ZIP_CPP_REPORT"),
    "mixed-export": ("--mixed-export-ui-case", "IMAGEJXX_MIXED_EXPORT_CPP_REPORT"),
    "import-limits": ("--roi-import-limits-ui-case", "IMAGEJXX_IMPORT_LIMITS_CPP_REPORT"),
    "roi-rebind": ("--roi-import-rebind-ui-case", "IMAGEJXX_ROI_REBIND_CPP_REPORT"),
    "roi-jobs": ("--roi-import-jobs-ui-case", "IMAGEJXX_ROI_JOBS_CPP_REPORT"),
    "roi-browse": ("--roi-import-browse-ui-case", "IMAGEJXX_ROI_BROWSE_CPP_REPORT"),
    "roi-queue": ("--roi-import-queue-ui-case", "IMAGEJXX_ROI_QUEUE_CPP_REPORT"),
    "planar-annotation": ("--planar-annotation-ui-case", "IMAGEJXX_PLANAR_CPP_REPORT"),
    "planar-rebind": ("--planar-annotation-rebind-ui-case", "IMAGEJXX_PLANAR_REBIND_CPP_REPORT"),
    "planar-tools": ("--planar-annotation-tools-ui-case", "IMAGEJXX_PLANAR_TOOLS_CPP_REPORT"),
    "z-subset": ("--z-subset-ui-case", "IMAGEJXX_Z_SUBSET_CPP_REPORT"),
    "imagej-projection": ("--imagej-projection-ui-case", "IMAGEJXX_PROJECTION_CPP_REPORT"),
    "intensity-calibration": ("--intensity-calibration-ui-case", "IMAGEJXX_INTENSITY_CPP_REPORT"),
    "intensity-nonlinear": ("--intensity-nonlinear-ui-case", "IMAGEJXX_INTENSITY_NONLINEAR_CPP_REPORT"),
    "planar-region": ("--regional-z-subset-ui-case", "IMAGEJXX_PLANAR_REGION_CPP_REPORT"),
    "display-remap": ("--display-z-subset-ui-case", "IMAGEJXX_DISPLAY_REMAP_CPP_REPORT"),
    "axis-spacing": ("--axis-spacing-ui-case", "IMAGEJXX_SPACING_CPP_REPORT"),
    "slicer-resize": ("--slicer-resize-ui-case", "IMAGEJXX_SLICER_CPP_REPORT"),
    "calibrated-command": ("--calibrated-command-ui-case", "IMAGEJXX_CALIBRATED_COMMAND_CPP_REPORT"),
    "roi-manager": ("--scientific-roi-manager-ui-case", "IMAGEJXX_ROI_MANAGER_CPP_REPORT"),
}


def tiff_cases():
    """Input gestures and fixture paths for the existing independent TIFF readers."""
    def scientific(group, case, folder, filename):
        return group, case, ["--scientific-tiff-ui-case", f"{folder}/{filename}"]

    def floating(group, case, preset, folder, filename):
        return group, case, ["--float32-tiff-ui-case", preset, f"{folder}/{filename}"]

    for order in ("le", "be"):
        yield scientific("uint8-tiff", order, "uint8-tiff-imagej154r", f"byte-{order}.tif")
        yield scientific("tiled-tiff", f"cpp-{order}001", "scientific-tiff-tiled",
                         f"edges-{order}-{'poison' if order == 'le' else 'zero'}.tif")
        for channels in (1, 2):
            case = f"c{channels}-{order}"
            yield scientific("rgb-tiff", case, "rgb-tiff-imagej154r", f"rgb-{case}.tif")
        for dtype in ("u8", "u16", "rgb8"):
            case = f"{dtype}-{order}"
            for codec, group in ((8, "deflate-tiff"), (32773, "packbits-tiff"), (5, "lzw-tiff")):
                yield scientific(group, case, "scientific-tiff-" + group.split("-")[0],
                                 f"{case}-r64-c{codec}.tif")
            for codec in (8, 5):
                yield scientific("horizontal-tiff", f"{case}-c{codec}", "scientific-tiff-horizontal",
                                 f"{case}-r2-c{codec}.tif")
    presets = {"TF01": "finite-le.tif", "TF02": "quiet-be.tif", "TF03": "ieee-le.tif",
               "TF04": "ieee-be.tif", "TF05": "quiet-le.tif", "TF06": "ieee-le.tif"}
    for preset, filename in presets.items():
        for group, folder in (("float32-tiff", "float32-tiff-imagej154r"),
                              ("deflate-float32", "deflate-float32-tiff-imagej154r"),
                              ("packbits-float32", "packbits-float32-tiff-imagej154r"),
                              ("lzw-float32", "lzw-float32-tiff-imagej154r")):
            yield floating(group, preset, preset, folder, filename)
        for codec in (8, 5):
            yield floating("floating-tiff", f"c{codec}/{preset}", preset,
                           "floating-float32-tiff-corpus", filename.replace(".tif", f"-c{codec}.tif"))
    fixtures = Path(__file__).resolve().parents[1] / "tests/fixtures"
    folder = "scientific-tiff-tiled-layouts"
    rows = json.loads((fixtures / folder / "SOURCE.json").read_bytes())["files"]
    for row in rows:
        if (row["valid"] and "-edges-" in row["file"]
                and row["pattern"] == "finite" and row["padding"]):
            yield scientific("tiled-layouts", "cpp-" + Path(row["file"]).stem + "001",
                             folder, row["file"])
    for family in ("deflate", "packbits-lzw", "predicted"):
        folder = f"scientific-tiff-{family}-tiles"
        rows = json.loads((fixtures / folder / "SOURCE.json").read_bytes())["files"]
        for row in rows:
            if (not row["valid"] or "-edges-" not in row["file"] or not row["padding"]
                    or (family == "deflate" and row["compression"] != 8)):
                continue
            if row["pattern"] == "finite":
                yield scientific(f"{family}-tiles", "cpp-" + Path(row["file"]).stem + "001",
                                 folder, row["file"])
            elif row["dtype"] == "f32" and row["pattern"] in ("quiet", "ieee"):
                preset = "TF02" if row["pattern"] == "quiet" else "TF04"
                codec = "" if family == "deflate" else f"-c{row['compression']}"
                case = f"float-{preset}-{row['endian']}{codec}001"
                yield floating(f"{family}-tiles-float", case, preset, folder, row["file"])


def shadow_preset_cases():
    directions = ("north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest")
    return [f"{mode}.{direction}" for mode in ("ordinary", "stack", "scientific") for direction in directions] + [
        "guard." + name for name in ("ordinary-f64", "scientific-f64", "scientific-rgba", "scientific-ieee")] + ["failure.float32"]


def shadow_demo_cases():
    return ["byte", "short", "float", "rgb", "escape", "stop", "source", "budget",
            "overflow", "guard-stack", "guard-type", "guard-policy"]


PATH_SEGMENT_MODES = ("active", "saved", "xy", "xz", "yz")
PATH_AUTHORING_MODES = ("active", "scientific-active", "saved", "xy", "xz", "yz")
PATH_FILTER_CASES = ("draw-gaussian",) + tuple(
    f"rank-{method}-{destination}"
    for method in ("minimum", "maximum", "median", "mean", "variance")
    for destination in ("in-place", "new-document")) + tuple(
    f"scientific-{operation}-{roi}"
    for operation in ("gaussian", "rank-mean") for roi in ("active", "positioned")) + ("budget-gaussian", "budget-rank-mean")
PATH_SPECIALIZED_FILTER_CASES = ("specialized-despeckle",) + tuple(
    f"specialized-outliers-{which}-{lut}" for which in ("bright", "dark")
    for lut in ("grays", "inverted")) + tuple(
    f"specialized-top-hat-{background}-{subtract}-{lut}"
    for background in ("dark", "light") for subtract in ("subtract", "no-subtract")
    for lut in ("grays", "inverted")) + tuple(
    f"specialized-remove-nans-{policy}" for policy in ("finite", "quiet", "ieee"))
PATH_FILTER_CASES += PATH_SPECIALIZED_FILTER_CASES
PATH_FILTER_LIFECYCLE_CASES = tuple(
    f"lifecycle-{operation}-{kind}" for operation in ("gaussian", "rank-mean")
    for kind in ("supersede", "invalid-editor", "owner-replace", "selection-revision", "pixel-revision", "calibration-revision")) + ("lifecycle-gaussian-invalid-external",)
PATH_FILTER_CASES += PATH_FILTER_LIFECYCLE_CASES
PATH_EMPTY_FILTER_CASES = tuple(
    f"empty-{operation}-{kind}" for operation in ("gaussian", "rank-mean")
    for kind in ("active-zero-area", "active-one-axis-zero", "active-off-canvas", "positioned-unmatched"))
PATH_FILTER_CASES += PATH_EMPTY_FILTER_CASES
PATH_FILTER_ADMISSION_CASES = (
    "admission-legacy-invert-path", "admission-pooled-mask-mean-path",
    "admission-pooled-display-mean-path", "admission-workflow-later-path-refusal")
PATH_FILTER_CASES += PATH_FILTER_ADMISSION_CASES
PATH_FILTER_STACK_CASES = tuple(
    f"stack-{operation}-{choice}" for operation in ("gaussian", "rank-mean")
    for choice in ("yes", "no", "cancel"))
PATH_FILTER_CASES += PATH_FILTER_STACK_CASES


PATH_AUTHORING_EDGE_CASES = ("input-ownership", "pointer-recovery-dpi", "numeric-stale", "budget", "extent-rebind")


def path_authoring_edge_cases():
    for case in PATH_AUTHORING_EDGE_CASES:
        for carrier in (PATH_AUTHORING_MODES[:2] if case == "extent-rebind" else
                        PATH_AUTHORING_MODES + ("saved-linked",) if case == "budget" else PATH_AUTHORING_MODES):
            yield case, carrier


SETTINGS_WAND_CASES = ("wand-consume", "wand-drafts", "wand-persist", "wand-reopen")
MANUAL_WORKFLOW_CASES = ("manual-workflow-current-path", "manual-workflow-all-z-path",
                         "manual-workflow-refusal-recovery")
SETTINGS_SELECTION_CASES = ("enlargement-consume-ordinary", "enlargement-consume-scientific",
                            "enlargement-persist", "enlargement-reopen")
SETTINGS_MEASUREMENT_CASES = ("measurement-names-edit-persist", "measurement-names-reopen")
SETTINGS_WAND_STATES = {
    "wand-consume": {"before", "default_selected", "eight_applied", "eight_selected",
                     "tolerant_applied", "tolerant_selected", "four_selected"},
    "wand-drafts": {"opened", "invalid_draft", "refused", "corrected", "reverted",
                    "closed_reopened", "accepted", "defaults_draft", "defaults_applied"},
    "wand-persist": {"draft", "saved", "closed"},
    "wand-reopen": {"loaded", "selected", "shown"},
}


def settings_wand_preferences_fixture():
    """Opaque source fixture only; UI readers own the independent expectations."""
    return {
        "format": "imagejxx-preferences", "version": 1,
        "values": {"wand_tolerance": 0.0, "wand_connectivity": 4,
                   "cfg1_future": {"items": [True, None, "λ", 2**64 - 1]}},
        "extras": {"recent": [], "cfg1_extra": {"label": "μ", "values": [3, 1]}},
        "cfg1_root": {"note": "root", "uint": 2**64 - 1},
    }


def settings_wand_observation(raw, case):
    """Validate the bounded transport envelope, independently of mask acceptance."""
    if not 0 < len(raw) < 16 * 1024 * 1024:
        raise ValueError("Wand observation byte limit")
    def unique(pairs):
        value = {}
        for key, item in pairs:
            if key in value:
                raise ValueError("Duplicate Wand observation key")
            value[key] = item
        return value
    def invalid(token):
        raise ValueError("Nonfinite Wand JSON token: " + token)
    row = json.loads(raw, object_pairs_hook=unique, parse_constant=invalid)
    pending, count = [(row, 0)], 0
    while pending:
        value, depth = pending.pop()
        count += 1
        if count > 500000 or depth > 64:
            raise ValueError("Wand observation structure limit")
        if isinstance(value, float) and not math.isfinite(value):
            raise ValueError("Nonfinite Wand observation number")
        if isinstance(value, dict):
            pending.extend((item, depth + 1) for item in value.values())
        elif isinstance(value, list):
            pending.extend((item, depth + 1) for item in value)
    if (not isinstance(row, dict) or row.get("schema") != "cfg1.settings-wand.ui.001"
            or row.get("case_id") != case or row.get("context") != "imgui_cpp"
            or row.get("status") != "observed" or row.get("error") != ""
            or row.get("acceptance_claim") is not False
            or not isinstance(row.get("states"), dict)
            or set(row["states"]) != SETTINGS_WAND_STATES[case]
            or not isinstance(row.get("frames"), list) or not 0 < len(row["frames"]) <= 750
            or not row.get("events")):
        raise ValueError("Unexpected Wand observation envelope")
    return row


def cases():
    for case in SETTINGS_MEASUREMENT_CASES:
        yield "settings-measurements", case, ["--settings-measurement-ui-case", case]
    for case in SETTINGS_SELECTION_CASES:
        yield "settings-selection", case, ["--settings-selection-ui-case", case]
    for case in MANUAL_WORKFLOW_CASES:
        yield "manual-workflow", case, ["--manual-threshold-workflow-ui-case", case]
    for case in SETTINGS_WAND_CASES:
        yield "settings-wand", case, ["--settings-wand-ui-case", case]
    for case in PATH_FILTER_CASES:
        yield "path-filters", case, ["--path-filter-ui-case", case]
    for case, carrier in path_authoring_edge_cases():
        yield "path-authoring-edges", case + "." + carrier, ["--path-authoring-edge-ui-case", case, carrier]
    for mode in ("ordinary", "linked", "workflow"):
        yield "root-paths", mode, ["--root-path-ui-case", mode]
    for mode in PATH_AUTHORING_MODES:
        yield "path-authoring", mode, ["--path-authoring-ui-case", mode]
    for mode in PATH_SEGMENT_MODES:
        yield "path-segments", mode, ["--path-segment-ui-case", mode]
    groups = [
        ("rank", "--rank-ui-case", [f"R{i:02d}" for i in range(1, 60)]),
        ("selection", "--selection-creation-ui-case", ["R01", "R02", "R03", "Q01", "Q02", "Q03", "Q04"]),
        ("selection-legacy", "--selection-creation-ui-test", [None]),
        ("regional-rotation", "--saved-roi-geometry-ui-case", ["PX11"]),
        ("rounded-export", "--rounded-roi-export-ui-case",
         ["axis", "rotated", "oversized", "zero", "precision"]),
        ("legacy-export", "--roi-export-ui-case", [None]),
        ("rounded-import", "--rounded-roi-import-ui-case", ["axis", "rotated", "zero", "oversized"]),
        ("legacy-replacement", "--roi-import-replacement-ui-case", [None]),
        ("rounded-legacy", "--rounded-legacy-ui-case", ["axis", "rotated"]),
        ("saved-budget", "--saved-roi-budget-ui-case",
         ["finite-axis", "finite-rotated", "ieee-axis", "ieee-rotated"]),
        ("saved-pointer", "--saved-roi-pointer-budget-ui-case",
         ["finite-axis", "finite-rotated", "ieee-axis", "ieee-rotated"]),
        ("saved-modifiers", "--saved-roi-pointer-budget-ui-case",
         [case + "-modifiers" for case in ["finite-axis", "finite-rotated", "ieee-axis", "ieee-rotated"]]),
        ("shadows", "--shadow-ui-case",
         ["north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest"]),
        ("scientific-shadows", "--scientific-shadow-ui-case",
         ["north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest"]),
        ("shadow-presets", "--shadow-preset-ui-case", shadow_preset_cases()),
        ("shadow-demo", "--shadow-demo-ui-case", shadow_demo_cases()),
        ("animation", "--animation-ui-case", ["z", "c", "t", "legacy", "singleton", "switch",
                                                "edit", "close", "stop", "fps", "ieee", "rgb", "busy", "range", "reverse", "ping",
                                                "range-offgrid", "range-c", "range-t", "range-invalid",
                                                  "cal-s", "cal-ms", "cal-us", "cal-step", "cal-fps",
                                                  "cal-invalid", "cal-edit", "cal-unit",
                                                  "cal-legacy", "cal-legacy-step", "cal-legacy-reverse",
                                                  "cal-legacy-ping", "cal-legacy-edit", "cal-legacy-unit"]),
        ("target-histogram", "--animation-ui-case", ["target-histogram-"+dtype+"-"+roi for dtype in ["u8","u16","f32"] for roi in ["whole","active","positioned"]]),
        ("histogram", "--animation-ui-case", ["histogram-"+dtype+"-"+roi for dtype in ["u8","u16","f32"] for roi in ["whole","roi","empty"]]),
        ("scientific-threshold-target", "--animation-ui-case", ["target-threshold-"+m+"-"+roi for m in ["default","ij_isodata","huang","huang2","renyi_entropy","shanbhag"] for roi in ["whole","active","positioned"]]),
        ("pooled-display", "--animation-ui-case", ["auto-display-"+m+"-"+mode for m in ["default","ij_isodata","huang","huang2","renyi_entropy","shanbhag"] for mode in ["pool","pooldark","poolboth"]]),
        ("automatic-display", "--animation-ui-case", ["auto-display-"+m+"-"+mode for m in ["default","ij_isodata","huang","huang2","renyi_entropy","shanbhag"] for mode in ["bright","dark","both"]]),
        ("pooled-threshold", "--animation-ui-case", ["threshold-"+m+o+"-pool" for m in ["default","ij_isodata","huang","huang2","renyi_entropy","shanbhag"] for o in ["","-ignore-both"]]),
        ("threshold-options", "--animation-ui-case", ["threshold-"+m+"-ignore-"+o for m in ["default","ij_isodata","huang","huang2","renyi_entropy","shanbhag"] for o in ["black","white","both"]]),
        ("auto-threshold", "--animation-ui-case", ["threshold-"+m for m in ["default","ij_isodata","huang","huang2","renyi_entropy","shanbhag"]]),
        ("movie", "--animation-ui-case", ["movie-z", "movie-t", "movie-c", "movie-reverse", "movie-ping", "movie-rgb", "movie-composite", "movie-existing", "movie-stale", "movie-cancel", "movie-invalid", "movie-f32", "movie-ieee", "movie-gray8", "movie-gray-refuse", "movie-draft"]),
        ("planar-matrix", "--planar-annotation-matrix-ui-case", planar_cases()),
        ("ieee-viewing", "--ieee-viewing-ui-case", [f"IEEE{i:02d}" for i in range(1, 5)]),
        ("native-workflow", "--native-workflow-ui-case", [None]),
        ("native-batch", "--native-workflow-batch-ui-case", [None]),
        ("headless-session", "--headless-session-consumer-test", [None]),
    ]
    groups.extend((group, flag, variants) for group, (flag, _, _, variants) in INDEXED_READERS.items())
    groups.extend((group, flag, [None]) for group, (flag, _) in EDITOR_READERS.items())
    groups.append(("scientific-threshold", "--manual-threshold-ui-case", ["T10"]))
    for group, flag, variants in groups:
        for variant in variants:
            yield group, variant or "legacy", [flag] + ([variant] if variant else [])

    for case, arguments in bigtiff_cases():
        yield "bigtiff", case, arguments
    yield from tiff_cases()
    for policy in ["whole", "positioned"]:
        case="histogram-recorder-"+policy
        yield "histogram-recorder",case,["--animation-ui-case",case]
    for mode in ["cancel","draft","source","close","replay","targets"]:
        case="histogram-async-"+mode
        yield "histogram-async",case,["--animation-ui-case",case]
    for mode in ["run","captured","readonly","disabled","preview","conflict","stale","cancel","batch"]:
        yield "histogram-workflow",mode,["--histogram-workflow-ui-case",mode]
    for mode in ["choices","raw_choices","restore","invalid_choices","authored"]:
        yield "histogram-workflow",mode,["--histogram-workflow-ui-case",mode]
    for mode in ["batch_statistics","batch_statistics_review","batch_statistics_conflict","batch_statistics_cancel","batch_statistics_setup"]:
        yield "histogram-workflow",mode,["--histogram-workflow-ui-case",mode]


    for mode in ["virtual_cancel","virtual_source","virtual_replay","virtual_xyz"]:
        case="histogram-async-"+mode
        yield "histogram-async",case,["--animation-ui-case",case]

    for mode in ["open","replay","cancel","changed","startup"]:
        yield "virtual-tiff",mode,["--virtual-tiff-ui-case",mode]

def reader_environment(output):
    reports = Path(output).resolve() / "reports"
    environment = {
        "IMAGEJXX_VIRTUAL_TIFF_CPP_ROOT": reports / "virtual-tiff",
        "IMAGEJXX_ROUNDED_CPP_CASE": reports / "selection/R01",
        "IMAGEJXX_REGIONAL_CREATION_CPP_BASE": reports / "selection",
        "IMAGEJXX_CREATION_WORKFLOW_CPP_REPORT": reports / "selection/Q04/case.json",
        "IMAGEJXX_REGION_ROTATION_CPP_REPORT": reports / "regional-rotation/PX11/case.json",
        "IMAGEJXX_ROUNDED_SAVED_CPP_BASE": reports / "selection",
        "IMAGEJXX_ROUNDED_EXPORT_CPP_BASE": reports / "rounded-export",
        "IMAGEJXX_ROI_EXPORT_CPP_REPORT": reports / "legacy-export/legacy/case.json",
        "IMAGEJXX_ROUNDED_IMPORT_CPP_BASE": reports / "rounded-import",
        "IMAGEJXX_ROI_REPLACEMENT_CPP_REPORT": reports / "legacy-replacement/legacy/case.json",
        "IMAGEJXX_ROUNDED_LEGACY_CPP_BASE": reports / "rounded-legacy",
        "IMAGEJXX_SAVED_BUDGET_CPP_BASE": reports / "saved-budget",
        "IMAGEJXX_SAVED_POINTER_CPP_BASE": reports / "saved-pointer",
        "IMAGEJXX_SAVED_MODIFIERS_CPP_BASE": reports / "saved-modifiers",
        "IMAGEJXX_SHADOW_CPP_BASE": reports / "shadows",
        "IMAGEJXX_SCIENTIFIC_SHADOW_CPP_BASE": reports / "scientific-shadows",
        "IMAGEJXX_SHADOW_PRESET_CPP_BASE": reports / "shadow-presets",
        "IMAGEJXX_SHADOW_DEMO_CPP_BASE": reports / "shadow-demo",
        "IMAGEJXX_PLANAR_MATRIX_CPP_ROOT": reports / "planar-matrix",
        "IMAGEJXX_IEEE_CPP_DIR": reports / "ieee-viewing",
        "IMAGEJXX_BIGTIFF_CPP_DIR": reports / "bigtiff",
        "IMAGEJXX_NATIVE_WORKFLOW_CPP_REPORT": reports / "native-workflow/legacy/case.json",
        "IMAGEJXX_NATIVE_BATCH_CPP_REPORT": reports / "native-batch/legacy/case.json",
        "IMAGEJXX_VARIANCE_CPP_REPORT": reports / "rank/R14/case.json",
        "IMAGEJXX_ANIMATION_CPP_ROOT": reports / "animation",
        "IMAGEJXX_MOVIE_CPP_ROOT": reports / "movie",
        "IMAGEJXX_THRESHOLD_CPP_ROOT": reports / "auto-threshold",
        "IMAGEJXX_THRESHOLD_OPTIONS_CPP_ROOT": reports / "threshold-options",
        "IMAGEJXX_POOLED_THRESHOLD_CPP_ROOT": reports / "pooled-threshold",
        "IMAGEJXX_TARGET_HISTOGRAM_CPP_ROOT": reports / "target-histogram",
        "IMAGEJXX_HISTOGRAM_RECORDER_CPP_ROOT": reports / "histogram-recorder",
        "IMAGEJXX_HISTOGRAM_ASYNC_CPP_ROOT": reports / "histogram-async",
        "IMAGEJXX_HISTOGRAM_WORKFLOW_CPP_ROOT": reports / "histogram-workflow",
        "IMAGEJXX_HISTOGRAM_CPP_ROOT": reports / "histogram",
        "IMAGEJXX_SCIENTIFIC_THRESHOLD_TARGET_CPP_ROOT": reports / "scientific-threshold-target",
        "IMAGEJXX_POOLED_DISPLAY_CPP_ROOT": reports / "pooled-display",
        "IMAGEJXX_AUTOMATIC_DISPLAY_CPP_ROOT": reports / "automatic-display",
    }
    environment["IMAGEJXX_THRESHOLD_CPP_REPORT"] = reports / "scientific-threshold/T10/case.json"
    # Session and selection rows bind their absolute capture directory. Readers use
    # original root; retain byte-identical transport copies for archival only.
    environment["IMAGEJXX_SESSION_CPP_REPORT"] = Path(output).resolve() / "private/headless-session/legacy/capture/imgui-cpp.json"
    environment["IMAGEJXX_SELECTION_CREATION_CPP_REPORT"] = Path(output).resolve() / "private/selection-legacy/legacy/capture/imgui-cpp.json"
    for group, (_, name, _, _) in INDEXED_READERS.items():
        environment[name] = Path(output).resolve() / (group + "-index.json")
    for group, (_, name) in EDITOR_READERS.items():
        environment[name] = reports / group / "legacy/case.json"
    for group, (name, _) in TIFF_READERS.items():
        environment[name] = reports / group
    for name in ("NONBYTE", "THRESHOLD", "CHANNEL_VARIANCE", "DESPECKLE",
                 "OUTLIERS", "REMOVE_NANS", "TOPHAT", "IEEE_REMOVE_NANS"):
        environment[f"IMAGEJXX_{name}_CPP_DIR"] = reports / "rank"
    environment["IMAGEJXX_PATH_SEGMENT_CPP_REPORTS"] = json.dumps({
        mode: str(reports / "path-segments" / mode / "case.json")
        for mode in PATH_SEGMENT_MODES
    })
    environment["IMAGEJXX_PATH_AUTHORING_EDGE_CPP_REPORTS"] = json.dumps({
        case + "/" + carrier: str(reports / "path-authoring-edges" / (case + "." + carrier) / "case.json")
        for case, carrier in path_authoring_edge_cases()
    })
    environment["IMAGEJXX_ROOT_PATH_CPP_REPORTS"] = json.dumps({
        mode: str(reports / "root-paths" / mode / "case.json")
        for mode in ("ordinary", "linked", "workflow")
    })
    environment["IMAGEJXX_PATH_AUTHORING_CPP_REPORTS"] = json.dumps({
        mode: str(reports / "path-authoring" / mode / "case.json")
        for mode in PATH_AUTHORING_MODES
    })
    environment["IMAGEJXX_PATH_FILTER_CPP_REPORTS"] = json.dumps({
        case: str(reports / "path-filters" / case / "case.json")
        for case in PATH_FILTER_CASES
    })
    environment["IMAGEJXX_SETTINGS_WAND_CPP_REPORTS"] = json.dumps({
        case: str(reports / "settings-wand" / case / "case.json")
        for case in SETTINGS_WAND_CASES
    })
    environment["IMAGEJXX_MANUAL_WORKFLOW_CPP_REPORTS"] = json.dumps({
        case: str(reports / "manual-workflow" / case / "case.json")
        for case in MANUAL_WORKFLOW_CASES
    })
    environment["IMAGEJXX_SETTINGS_SELECTION_CPP_REPORTS"] = json.dumps({
        case: str(reports / "settings-selection" / case / "case.json")
        for case in SETTINGS_SELECTION_CASES
    })
    environment["IMAGEJXX_SETTINGS_MEASUREMENT_CPP_REPORTS"] = json.dumps({
        case: str(reports / "settings-measurements" / case / "case.json")
        for case in SETTINGS_MEASUREMENT_CASES
    })
    return {name: str(path) for name, path in environment.items()}


def inventory(directory):
    result = []
    size = 0
    for path in sorted(directory.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"Unexpected symlink in C++ capture: {path}")
        if not path.is_file():
            continue
        size += path.stat().st_size
        if size > 256 * 1024 * 1024 or len(result) >= 4096:
            raise ValueError(f"C++ capture exceeds retention budget: {directory}")
        result.append({"member": path.relative_to(directory).as_posix(), **pin(path)})
    return result


def write_case_index(output, group, items, elapsed):
    """Index complete raw captures with byte identities and relative paths only."""
    _, _, contract, expected = INDEXED_READERS[group]
    if (tuple(item["case"] for item in items) != expected
            or any(item["state"] != "observed" for item in items)
            or not math.isfinite(elapsed) or not 0 <= elapsed <= 60):
        raise ValueError(f"Incomplete or out-of-bounds indexed capture: {group}")
    parent = Path(output).resolve()
    group_root = parent / "private" / group
    entries = []
    for item in items:
        path = Path(item["actual"]) / item["report"]
        if path.is_symlink() or not path.resolve(strict=True).is_relative_to(group_root):
            raise ValueError(f"Indexed report is outside its group: {path}")
        identity = pin(path)
        original = next(row for row in item["files"] if row["member"] == item["report"])
        if identity != {key: original[key] for key in ("bytes", "sha256")}:
            raise ValueError(f"Indexed report changed after capture: {path}")
        if not 0 < identity["bytes"] <= 16 * 1024 * 1024:
            raise ValueError(f"Indexed report exceeds its reader budget: {path}")
        entries.append({"case_id": item["case"], "id": item["case"],
                        "relative_path": path.relative_to(parent).as_posix(),
                        "byte_count": identity["bytes"], "sha256": identity["sha256"]})
    index = {"contract_id": contract, "context": "imgui_cpp", "case_count": len(entries),
             "observation_state": "observed", "acceptance_claim": False,
             "elapsed_seconds": elapsed, "cases": entries}
    path = parent / (group + "-index.json")
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(index, indent=2) + "\n")
    return {"path": str(path), **pin(path)}


def select_cases(selected_cases=None):
    """Resolve declared routes before launch; preserve the complete default."""
    available = list(cases())
    if selected_cases is None:
        return available
    if not isinstance(selected_cases, list) or not selected_cases:
        raise ValueError("Focused observation selection must be nonempty")
    keys = []
    for row in selected_cases:
        if (not isinstance(row, (list, tuple)) or len(row) != 2
                or any(not isinstance(value, str) or not value for value in row)):
            raise ValueError("Each observation selector requires group and case")
        keys.append(tuple(row))
    if len(set(keys)) != len(keys):
        raise ValueError("Duplicate observation selector")
    if ("settings-wand", "wand-reopen") in keys:
        persist = ("settings-wand", "wand-persist")
        if persist not in keys or keys.index(persist) > keys.index(("settings-wand", "wand-reopen")):
            raise ValueError("Wand reopen requires the preceding persist process")
    if ("settings-selection", "enlargement-reopen") in keys:
        persist = ("settings-selection", "enlargement-persist")
        if persist not in keys or keys.index(persist) > keys.index(("settings-selection", "enlargement-reopen")):
            raise ValueError("Selection reopen requires the preceding persist process")
    if ("settings-measurements", "measurement-names-reopen") in keys:
        persist = ("settings-measurements", "measurement-names-edit-persist")
        if persist not in keys or keys.index(persist) > keys.index(("settings-measurements", "measurement-names-reopen")):
            raise ValueError("Measurement reopen requires the preceding persist process")
    index = {(group, case): (group, case, arguments)
             for group, case, arguments in available}
    if set(keys) - set(index):
        raise ValueError(f"Unknown required observation routes: {sorted(set(keys) - set(index))}")
    for group in INDEXED_READERS:
        chosen = {case for candidate, case in keys if candidate == group}
        required = {case for candidate, case, _ in available if candidate == group}
        if chosen and chosen != required:
            raise ValueError(f"Indexed observation group must be complete: {group}")
    return [index[key] for key in keys]


def capture(output, executable, root, environment=None, *, selected_cases=None,
            case_timeout=120):
    selected = select_cases(selected_cases)
    if type(case_timeout) is not int or not 1 <= case_timeout <= 3600:
        raise ValueError("Observation timeout must be an integer in [1,3600]")
    output = Path(output).resolve()
    executable = Path(executable).resolve(strict=True)
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ValueError(f"C++ application is not executable: {executable}")
    if output.exists():
        raise ValueError(f"Use a fresh C++ capture directory: {output}")
    output.mkdir(parents=True)
    original_pin = pin(executable)
    receipt = {"state": "running", "acceptance_claim": False,
               "application": {"path": str(executable), **original_pin},
               "cases": [], "reader_environment": reader_environment(output)}

    def save():
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    save()
    indexed_starts = {}
    try:
        for group, case, arguments in selected:
            if group in INDEXED_READERS and group not in indexed_starts:
                indexed_starts[group] = time.monotonic()
            # main.cpp places preferences next to the capture directory. Sharing
            # that parent across cases would leak settings between probes.
            wand_pair = group == "settings-wand" and case in ("wand-persist", "wand-reopen")
            selection_pair = group == "settings-selection" and case in ("enlargement-persist", "enlargement-reopen")
            measurement_pair = group == "settings-measurements"
            preferences_pair = wand_pair or selection_pair or measurement_pair
            private = output / "private" / group / ("persisted-pair" if preferences_pair else case)
            private.mkdir(parents=True, exist_ok=preferences_pair)
            tiff = group == "bigtiff" or group in TIFF_READERS
            nested = group == "bigtiff" or (group in TIFF_READERS and TIFF_READERS[group][1])
            actual = private / ("persist_capture" if preferences_pair and case in ("wand-persist", "enlargement-persist", "measurement-names-edit-persist") else
                                "reopen_capture" if preferences_pair else
                                "capture001" if nested else "capture")
            transport = output / "reports" / group / case
            if nested:
                transport = transport / "capture001"
            if group == "settings-wand":
                preferences = private / "preferences.json"
                preferences.parent.mkdir(parents=True, exist_ok=True)
                if case == "wand-persist":
                    with preferences.open("x", encoding="utf-8", newline="\n") as stream:
                        stream.write(json.dumps(settings_wand_preferences_fixture(), ensure_ascii=False) + "\n")
                elif case == "wand-reopen" and not preferences.is_file():
                    raise ValueError("Wand reopen preferences are unavailable")
                argv = [str(executable), *arguments, str(actual), str(preferences)]
                preferences_before = pin(preferences) if preferences.exists() else None
            elif group in ("settings-selection", "settings-measurements"):
                preferences = private / "preferences.json"
                if case in ("enlargement-reopen", "measurement-names-reopen") and not preferences.is_file():
                    raise ValueError("Settings reopen preferences are unavailable")
                if case in ("enlargement-persist", "measurement-names-edit-persist") and preferences.exists():
                    raise ValueError("Settings persistence must start without a preference fixture")
                argv = [str(executable), *arguments, str(actual), str(preferences)]
                preferences_before = pin(preferences) if preferences.exists() else None
            elif tiff:
                relative = "scientific-bigtiff/" + arguments[-1] if group == "bigtiff" else arguments[-1]
                fixture = Path(root) / "tests/fixtures" / relative
                manifest = fixture.parent / "SOURCE.json"
                rows = json.loads(manifest.read_bytes())["files"]
                row = next(row for row in rows if row["file"] == fixture.name)
                if fixture.stat().st_size > 64 * 1024 * 1024:
                    raise ValueError(f"TIFF fixture exceeds capture budget: {fixture}")
                fixture_pin = pin(fixture)
                expected = {"sha256": row["sha256"]}
                if "bytes" in row:
                    expected["bytes"] = row["bytes"]
                if any(fixture_pin[key] != value for key, value in expected.items()):
                    raise ValueError(f"TIFF fixture changed: {fixture}")
                argv = [str(executable), *arguments[:-1], str(actual), str(fixture)]
            elif group == "headless-session":
                argv = [str(executable), *arguments, "--consumer-dir", str(actual)]
            else:
                argv = [str(executable), *arguments, str(actual)]
            item = {"group": group, "case": case, "argv": argv, "state": "running",
                    "actual": str(actual), "transport": str(transport)}
            if group in ("settings-wand", "settings-selection", "settings-measurements"):
                item["preferences_path"] = str(preferences)
                item["preferences_before"] = preferences_before
                if group in ("settings-selection", "settings-measurements"):
                    item["capture_process_id"] = os.getpid()
            if tiff:
                item["inputs"] = [{"path": str(fixture), **fixture_pin},
                                  {"path": str(manifest), **pin(manifest)}]
            receipt["cases"].append(item)
            save()
            started = time.monotonic()
            print(f"C++ observation: {group}/{case}", flush=True)
            with (private / (case + ".process.log" if preferences_pair else "process.log")).open("wb") as log:
                process = subprocess.run(argv, cwd=root, env=environment, stdout=log,
                                         stderr=subprocess.STDOUT,
                                         timeout=min(case_timeout, 60) if group in ("settings-wand", "manual-workflow", "settings-selection", "settings-measurements") else case_timeout,
                                         check=False)
            item.update(exit_code=process.returncode, elapsed_seconds=time.monotonic() - started)
            if process.returncode:
                raise subprocess.CalledProcessError(process.returncode, argv)
            report = actual / ("report.json" if group == "ieee-viewing" else
                               "imgui-cpp.json" if group in ("headless-session", "selection-legacy") else "case.json")
            if (not report.is_file() or report.stat().st_size > 128 * 1024 * 1024
                    or (group in ("path-filters", "settings-wand", "manual-workflow", "settings-selection", "settings-measurements") and report.stat().st_size >= 16 * 1024 * 1024)):
                raise ValueError(f"Missing or oversized observation report: {report}")
            observation = (settings_wand_observation(report.read_bytes(), case)
                           if group == "settings-wand" else json.loads(report.read_bytes()))
            if group == "settings-wand":
                item["settings_wand_schema"] = observation["schema"]
                item["preferences_after"] = pin(preferences) if preferences.exists() else None
                if case == "wand-reopen" and item["preferences_after"] != preferences_before:
                    raise ValueError("Wand reopen changed the persisted preference bytes")
            elif group in ("settings-selection", "settings-measurements"):
                expected_schema = ("cfg3.measurement-names.ui.001" if group == "settings-measurements"
                                   else "cfg2.settings-selection.ui.001")
                if (observation.get("schema") != expected_schema
                        or observation.get("case_id") != case or observation.get("context") != "imgui_cpp"
                        or observation.get("status") != "observed" or observation.get("error") != ""
                        or observation.get("acceptance_claim") is not False
                        or not observation.get("states") or not observation.get("events")
                        or not 0 < len(observation.get("frames", [])) <= 750):
                    raise ValueError(f"Unexpected settings observation: {report}")
                item["settings_measurement_schema" if group == "settings-measurements" else "settings_selection_schema"] = observation["schema"]
                runtime = actual / "runtime.json"
                identity = json.loads(runtime.read_bytes())
                if (identity != observation.get("fixture", {}).get("runtime")
                        or type(identity.get("process_id")) is not int or identity["process_id"] <= 0
                        or identity.get("parent_process_id") != item["capture_process_id"]):
                    raise ValueError("Settings C++ runtime is not the launched child")
                item["runtime"] = {"path": str(runtime), **pin(runtime), **identity}
                item["preferences_after"] = pin(preferences) if preferences.exists() else None
                if case in ("enlargement-reopen", "measurement-names-reopen") and item["preferences_after"] != preferences_before:
                    raise ValueError("Settings reopen changed persisted preference bytes")
                if case in ("enlargement-persist", "measurement-names-edit-persist") and item["preferences_after"] is None:
                    raise ValueError("Settings persistence did not save preferences")
            elif group == "headless-session":
                rows = observation.get("cases")
                if (observation.get("schema_version") != 1
                        or observation.get("contract_id") != "p02a.headless-session.001"
                        or observation.get("context") != "imgui_cpp"
                        or observation.get("status") != "passed"
                        or observation.get("acceptance_claim") is not False
                        or not isinstance(rows, list)
                        or any(not isinstance(row, dict) for row in rows)
                        or [row.get("case_id") for row in rows] != [f"S{i:02d}" for i in range(1, 9)]
                        or any(row.get("context") != "imgui_cpp"
                               or row.get("contract_id") != "p02a.headless-session.001"
                               or row.get("status") != "passed"
                               or row.get("acceptance_claim") is not False
                               or not row.get("frames")
                               or not isinstance(row.get("lifecycle"), list) for row in rows)):
                    raise ValueError(f"Persistent session probe did not finish: {report}")
                item["legacy_probe_envelope"] = True
            elif group == "ieee-viewing":
                # The existing IEEE probe predates the observation vocabulary.
                # Preserve its raw report; its "passed" only means probe completion.
                if (observation.get("state") != "passed" or observation.get("case_id") != case
                        or observation.get("context") != "imgui_cpp"
                        or not all(key in observation for key in ("frames", "captures", "source_words"))
                        or observation.get("acceptance_claim", False) is not False):
                    raise ValueError(f"IEEE probe did not finish: {report}")
                item["legacy_probe_state"] = "passed"
            elif group in ("rgb-dataset", "scientific-process"):
                # These two existing probes predate the acceptance_claim field.
                # Validate their distinct envelopes, preserve raw bytes, and let
                # the independent readers establish numerical/UI behavior.
                required = ("preview", "pixels", "indexed_type_conversion", "error") if group == "rgb-dataset" else ("before", "recorded")
                if (observation.get("context") != "imgui_cpp"
                        or not observation.get("frames") or not observation.get("controls")
                        or not all(key in observation for key in required)
                        or observation.get("acceptance_claim", False) is not False
                        or (group == "scientific-process" and (
                            observation.get("state") != "observed"
                            or observation.get("contract") != "scientific-process-review.ui.001"))):
                    raise ValueError(f"Legacy editor probe did not finish: {report}")
                item["legacy_probe_envelope"] = True
            elif group in ("annotation-workflow", "planar-path", "native-bare-import"):
                contracts = {"annotation-workflow": "p13d.annotation-workflow.ui.001",
                             "planar-path": "p13b.planar-path.ui.001",
                             "native-bare-import": "p13c.native-bare-import.ui.001"}
                # Earlier p13b/d producers have no acceptance field. Validate
                # their explicit observation envelope without rewriting bytes.
                if (observation.get("contract") != contracts[group]
                        or observation.get("context") != "imgui_cpp"
                        or observation.get("state") != "observed"
                        or observation.get("error") != ""
                        or not observation.get("frames") or not observation.get("controls")
                        or observation.get("acceptance_claim", False) is not False):
                    raise ValueError(f"Unexpected legacy path/workflow observation: {report}")
                item["legacy_path_contract"] = contracts[group]
            elif group == "root-paths":
                if (observation.get("contract") != "p13e.root-path.ui.001"
                        or observation.get("mode") != case
                        or observation.get("context") != "imgui_cpp"
                        or observation.get("state") != "observed"
                        or observation.get("error") != ""
                        or not observation.get("frames") or not observation.get("controls")
                        or observation.get("acceptance_claim", False) is not False):
                    raise ValueError(f"Unexpected root path observation: {report}")
                item["root_path_contract"] = observation["contract"]
            elif group == "path-authoring-edges":
                case_id, carrier = case.split(".", 1)
                if (observation.get("contract") != "p13h.path-authoring.edge-ui.001"
                        or observation.get("case_id") != case_id
                        or observation.get("carrier") != carrier
                        or observation.get("context") != "imgui_cpp"
                        or observation.get("state") != "observed"
                        or observation.get("error") != ""
                        or not observation.get("frames") or not observation.get("controls")
                        or observation.get("acceptance_claim") is not False):
                    raise ValueError(f"Unexpected path authoring edge observation: {report}")
                item["path_authoring_edge_contract"] = observation["contract"]
            elif group == "path-filters":
                if (observation.get("contract") != "p13i.path-filter.ui.001"
                        or observation.get("case_id") != case
                        or observation.get("context") != "imgui_cpp"
                        or observation.get("state") != "observed"
                        or observation.get("error") != ""
                        or not observation.get("frames") or not observation.get("controls")
                        or observation.get("acceptance_claim") is not False
                        or len(observation["frames"]) > 750):
                    raise ValueError(f"Unexpected path filter observation: {report}")
                item["path_filter_contract"] = observation["contract"]
            elif group == "manual-workflow":
                if (observation.get("schema") != "manual-threshold.workflow.ui.001"
                        or observation.get("case_id") != case
                        or observation.get("context") != "imgui_cpp"
                        or observation.get("status") != "observed"
                        or observation.get("error") != ""
                        or observation.get("acceptance_claim") is not False
                        or not observation.get("events") or not observation.get("states")
                        or not 0 < len(observation.get("frames", [])) <= 750):
                    raise ValueError(f"Unexpected manual workflow observation: {report}")
                item["manual_workflow_schema"] = observation["schema"]
            elif group == "path-authoring":
                if (observation.get("contract") != "p13h.path-authoring.ui.001"
                        or observation.get("mode") != case
                        or observation.get("context") != "imgui_cpp"
                        or observation.get("state") != "observed"
                        or observation.get("error") != ""
                        or not observation.get("frames") or not observation.get("controls")
                        or observation.get("acceptance_claim") is not False):
                    raise ValueError(f"Unexpected path authoring observation: {report}")
                item["path_authoring_contract"] = observation["contract"]
            elif group == "path-segments":
                if (observation.get("contract") != "p13g.path-segments.ui.001"
                        or observation.get("mode") != case
                        or observation.get("context") != "imgui_cpp"
                        or observation.get("state") != "observed"
                        or observation.get("error") != ""
                        or not observation.get("frames") or not observation.get("controls")
                        or observation.get("acceptance_claim", False) is not False):
                    raise ValueError(f"Unexpected path segment observation: {report}")
                item["path_segment_contract"] = observation["contract"]
            else:
                if observation.get("observation_state", observation.get("state")) != "observed":
                    raise ValueError(f"C++ observation did not finish: {report}")
                if observation.get("acceptance_claim") is not False:
                    raise ValueError(f"C++ observation must not claim acceptance: {report}")
            if group in INDEXED_READERS:
                if (observation.get("contract_id") != INDEXED_READERS[group][2]
                        or observation.get("case_id") != case
                        or observation.get("context") != "imgui_cpp"):
                    raise ValueError(f"Unexpected indexed observation identity: {report}")
            item["report"] = report.name
            item["files"] = inventory(actual)
            # Do not link captures: hard links change file identity/link counts,
            # and symlinks require privileges on some supported hosts.
            shutil.copytree(actual, transport)
            if inventory(transport) != item["files"]:
                raise ValueError(f"C++ observation copy changed bytes: {transport}")
            item["state"] = "observed"
            if group in INDEXED_READERS and case == INDEXED_READERS[group][3][-1]:
                identity = write_case_index(output, group,
                    [row for row in receipt["cases"] if row["group"] == group],
                    time.monotonic() - indexed_starts[group])
                receipt.setdefault("indices", {})[group] = identity
            save()
        if set(indexed_starts) != set(receipt.get("indices", {})):
            raise ValueError("Incomplete indexed observation group")
        if pin(executable) != original_pin:
            raise ValueError("C++ application changed during observation capture")
        receipt["state"] = "observed"
        return receipt["reader_environment"]
    except BaseException as error:
        receipt.update(state="failed", error={"type": type(error).__name__, "message": str(error)})
        if receipt["cases"] and receipt["cases"][-1]["state"] == "running":
            receipt["cases"][-1]["state"] = "failed"
        raise
    finally:
        save()


def archive(output):
    """Retain originals, transports, preferences and logs without rewriting JSON."""
    output = Path(output).resolve()
    destination = output.with_suffix(".zip")
    if destination.exists():
        raise ValueError(f"Refusing to replace capture archive: {destination}")
    with zipfile.ZipFile(destination, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=1) as bundle:
        for path in sorted(output.rglob("*")):
            if path.is_symlink():
                raise ValueError(f"Unexpected symlink in capture archive: {path}")
            if path.is_file():
                bundle.write(path, path.relative_to(output).as_posix())
    return {"path": str(destination), **pin(destination)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--application", type=Path, required=True)
    args = parser.parse_args()
    capture(args.output, args.application, Path(__file__).resolve().parents[1])
    print(json.dumps(archive(args.output), indent=2))


if __name__ == "__main__":
    main()
