"""Independent P05l entry checks for the pinned hidden-AWT Q01--Q17 capture.

No ImageJ, native/product geometry, capture script or observed result supplies
an expected value.  Literal stages below were derived from ImageJ 1.54r sources:
ImageCanvas.setRoiModState/mouseReleased, Roi.grow/growConstrained/modifyRoi/
abortModification, PolygonRoi construction and FreehandRoi.growFloat.  Masks are
independent integer cells, including the two small ellipse row-span tables.
P05e's existing 72 captures remain qualified only for their identical operands;
this file neither recaptures them nor gives them new gesture/lifetime credit.

Q14 characterizes existing-handle routing. Q15/Q16 characterize upstream
line-to-area/point-list behavior, which the owned area-base child refuses.
This is EDT handler dispatch plus offscreen paint, never OS input or a Meta
platform claim.  Missing capture data is a failure when this file is selected.
"""

from __future__ import annotations

from dataclasses import dataclass, replace
import hashlib
import json
import math
import os
from pathlib import Path

import pytest


CONTRACT = "p05l.selection-creation-reference.001"
CASE_IDS = tuple(f"Q{i:02d}" for i in range(1, 18))
EXPECTED_COLLECTION_COUNT = 17
EXPECTED_COLLECTION_IDS = tuple(f"test_selection_creation_reference[{i}]" for i in CASE_IDS)
EVENT_COUNTS = (4, 6, 6, 8, 6, 6, 6, 6, 6, 3, 7, 18, 8, 11, 6, 5, 8)
WIDTH, HEIGHT = 32, 24
MAX_BYTES = 16 * 1024**2
COMMIT = "62db90a269cccc1ac79f658e4602043214ed055b"
ARCHIVE_SHA = "09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665"
RUNTIME_SHA = "8e33a11c7e79eb02c055bcee40de806d16e59f0b42e58c4689bd23eb47d990bb"
CLASS_RECEIPT_SHA = "5360efa8a9f605d845aec107d6d9e1ee940e9732d0bdafdbd07a4c213d1bb471"
SOURCE_PINS = {
    "ij/gui/ImageCanvas.java": "ab27a57aa060ca8e297d59b09e478406ebd7a3bbd5d6a741bc1ab56c7cc553ab",
    "ij/gui/Roi.java": "200ad9f501a5860fe1486a3d2a8ed6cf6f82f7c7562a81e11d91ee7b7a04497e",
    "ij/gui/PolygonRoi.java": "1c5b398b5423c7f763144f12b95cfa23e08ef9e1cc07a93ba9873411f0f76038",
    "ij/gui/FreehandRoi.java": "c190ec16c3b027b50906d08926fc6ee7b8c0f6845e974b3a974ecc8c3accaba8",
    "ij/gui/OvalRoi.java": "f065f9a82d7e7d9de5c84615e8885465071f41076f872941f1b4259f93d76a38",
    "ij/gui/ShapeRoi.java": "8ab4a507fed1ddd09391c5663d482350c9f035e17788d915b440915277fc419e",
    "ij/gui/Line.java": "49aa00dd002627b17b4a287927b5c198a7ee136a788284d6850dccff95ddb67e",
    "ij/gui/PointRoi.java": "f77398d2822dc2235c06c4340f84ed4a2a7d8d5b7d573fe2870325990a16fd77",
    "ij/ImagePlus.java": "477e3f5f913b9c42feb68ea35d21836d45fd56920cccbfc5719a06822cbcfab9",
    "ij/ImageJ.java": "e043a86f66cbb32c2ab2866502b0e37436b639171697ee706bcab92219d26a59",
    "ij/process/ImageProcessor.java": "f98e87d25eb6b1f2c5d8557b73dd33e36fef0494d903d6d250206371dccc279c",
    "ij/process/PolygonFiller.java": "2d6c43bb6fed5bf206fa552bf0f60883117436884fcd9869ec4b9474ef7c5968",
}


# Independent fixed event spelling. K+/K- are real key-listener calls; P/D/U/M
# are real canvas mouse events. Coordinates are image integers, screen=image*8.
TIMELINES = {
    "Q01": ("P18,6", "D21,8", "D25,10", "U25,10"),
    "Q02": ("shift+", "P18,6", "D21,8", "D25,10", "U25,10", "shift-"),
    "Q03": ("alt+", "P8,7", "D10,8", "D12,10", "U12,10", "alt-"),
    "Q04": ("shift+", "alt+", "P18,6", "D21,8", "D25,10", "U25,10", "alt-", "shift-"),
    "Q05": ("shift+", "P18,6", "D21,8", "shift-", "D25,10", "U25,10"),
    "Q06": ("P18,6", "D21,8", "shift+", "D25,10", "U25,10", "shift-"),
    "Q07": ("shift+", "P10,7", "D13,9", "D17,11", "U17,11", "shift-"),
    "Q08": ("alt+", "P10,7", "D13,9", "D17,11", "U17,11", "alt-"),
    "Q09": ("shift+", "P18,6", "D21,8", "D25,10", "U25,10", "shift-"),
    "Q10": ("P18,6", "D21,8", "U25,10"),
    "Q11": ("shift+", "P18,6", "D25,10", "escape+", "escape-", "U25,10", "shift-"),
    "Q12": ("shift+", ("P18,4", "vertex.1.press"), ("U18,4", "vertex.1.release"), "shift-",
            ("M25,4", "vertex.2.move"), ("P25,4", "vertex.2.press"), ("U25,4", "vertex.2.release"),
            "alt+", ("M25,11", "vertex.3.move"), ("P25,11", "vertex.3.press"), ("U25,11", "vertex.3.release"),
            "alt-", ("M18,11", "vertex.4.move"), ("P18,11", "vertex.4.press"), ("U18,11", "vertex.4.release"),
            ("M18,4", "finish.move"), ("P18,4", "finish.press"), ("U18,4", "finish.release")),
    "Q13": ("alt+", "P8,7", "D12,7", "D12,10", "D8,10", "D8,7", "U8,7", "alt-"),
    "Q14": ("shift+", ("P4,4", "handle.press"), ("D7,6", "handle.drag.1"), ("U7,6", "handle.release"),
            "shift-", "reset", "alt+", ("P8,7", "interior.press"), ("D12,10", "interior.drag.1"),
            ("U12,10", "interior.release"), "alt-"),
    "Q15": ("shift+", "P18,6", "D21,8", "D25,10", "U25,10", "shift-"),
    "Q16": ("alt+", "P6,5", "D12,10", "U12,10", "alt-"),
    "Q17": ("shift+", "ctrl+", "P18,6", "D21,8", "D25,10", "U25,10", "ctrl-", "shift-"),
}


def _input(case_id):
    held, events, drag = set(), [], 0
    for raw in TIMELINES[case_id]:
        token, label = raw if isinstance(raw, tuple) else (raw, None)
        if token == "reset":
            events.append({"kind": "setup", "label": "baseline.reset", "method": "explicit_api_baseline_reset"})
            held.clear()
        elif token[-1] in "+-":
            name, down = token[:-1], token[-1] == "+"
            held.add(name) if down else held.discard(name)
            events.append({"kind": "key", "label": name + (".down" if down else ".up"),
                           "name": name, "down": down, "modifiers": sorted(held - {"escape"})})
        else:
            action = {"P": "press", "D": "drag", "U": "release", "M": "move"}[token[0]]
            if action == "drag":
                drag += 1
            events.append({"kind": "mouse", "label": label or (f"drag.{drag}" if action == "drag" else action),
                           "action": action, "image_point": [int(i) for i in token[1:].split(",")],
                           "button": "none" if action == "move" else "left",
                           "modifiers": sorted(held - {"escape"}), "click_count": 1})
    base = {"kind": "rectangle", "xywh": [4, 4, 10, 8]}
    if case_id in ("Q07", "Q08"):
        base = None
    elif case_id == "Q15":
        base = {"kind": "line", "points": [[4, 4], [14, 12]]}
    elif case_id == "Q16":
        base = {"kind": "point", "points": [[8, 7]]}
    return {"id": case_id, "tool": {"Q09": "ellipse", "Q12": "polygon", "Q13": "freehand"}.get(case_id, "rectangle"),
            "base": base, "canvas_wh": [32, 24], "source_shape": [2, 24, 32], "slice_index": 1,
            "magnification": 8, "source_rect_xywh": [0, 0, 32, 24], "events": events}


INPUTS = {case_id: _input(case_id) for case_id in CASE_IDS}


def _rect(box):
    x, y, w, h = box
    return frozenset((xx, yy) for yy in range(max(0, y), min(HEIGHT, y + h))
                     for xx in range(max(0, x), min(WIDTH, x + w)))


# OvalRoi.getMask evaluates ellipse membership at cell centers. These literal
# spans are independently worked out for the only two nonempty admitted sizes.
ELLIPSE_SPANS = {4: ((1, 3), (0, 4), (0, 4), (1, 3)),
                 8: ((2, 6), (1, 7), (0, 8), (0, 8), (0, 8), (0, 8), (1, 7), (2, 6))}


def _ellipse(box):
    x, y, w, h = box
    if w == h == 0:
        return frozenset()
    assert w == h and w in ELLIPSE_SPANS
    return frozenset((x + xx, y + yy) for yy, (lo, hi) in enumerate(ELLIPSE_SPANS[w]) for xx in range(lo, hi))


BASE_BOX = (4, 4, 10, 8)
BASE_PIXELS = _rect(BASE_BOX)
HOLE_BOX = (8, 7, 4, 3)
HOLE_PIXELS = _rect(HOLE_BOX)
# The thin source line's actual drawPixels/DDA marks differ from the area strip
# used by ShapeRoi(Roi.convertLineToArea(line)). They are intentionally separate.
LINE_DRAW = frozenset(((4, 4), (5, 5), (6, 6), (7, 6), (8, 7), (9, 8),
                       (10, 9), (11, 10), (12, 10), (13, 11), (14, 12)))
LINE_AREA = frozenset(((4, 4), (5, 5), (6, 5), (6, 6), (7, 6), (8, 7),
                       (9, 8), (10, 9), (11, 9), (11, 10), (12, 10), (13, 11)))
DRAG_BOXES = {
    "Q01": ((18, 6, 3, 2), (18, 6, 7, 4)),
    "Q02": ((18, 6, 4, 4), (18, 6, 8, 8)),
    "Q03": ((8, 7, 2, 1), HOLE_BOX),
    "Q04": ((18, 6, 4, 4), (18, 6, 8, 8)),
    "Q05": ((18, 6, 4, 4), (18, 6, 7, 4)),
    "Q06": ((18, 6, 3, 2), (18, 6, 8, 8)),
    "Q07": ((10, 7, 4, 4), (10, 7, 8, 8)),
    "Q08": ((10, 7, 3, 2), (10, 7, 7, 4)),
    "Q09": ((17, 5, 4, 4), (17, 4, 8, 8)),
    "Q10": ((18, 6, 3, 2),),
    "Q11": ((18, 6, 8, 8),),
    "Q14": (HOLE_BOX,),
    "Q15": ((18, 6, 4, 4), (18, 6, 8, 8)),
    "Q16": ((6, 5, 6, 5),),
    # Roi.grow's center branch reads the preceding ROI x/y. Thus these two
    # literal stages cannot be replaced with a one-shot square calculation.
    "Q17": ((16, 4, 4, 4), (14, 2, 8, 8)),
}
LATCHED_MODE = {"Q02": 1, "Q03": 2, "Q04": 1, "Q05": 1, "Q09": 1,
                "Q11": 1, "Q12": 1, "Q13": 2, "Q14": 2, "Q15": 1, "Q16": 2, "Q17": 1}
FINAL_BOXES = {"Q02": (4, 4, 22, 10), "Q03": BASE_BOX, "Q04": (4, 4, 22, 10),
               "Q05": (4, 4, 21, 8), "Q09": (4, 4, 21, 8), "Q11": BASE_BOX,
               "Q12": (4, 4, 21, 8), "Q13": BASE_BOX, "Q14": BASE_BOX,
               "Q15": (3, 3, 23, 11), "Q17": (4, 2, 18, 10)}


@dataclass(frozen=True)
class ExpectedRoi:
    kind: str
    box: tuple
    pixels: frozenset
    state: int = 3
    mode: int = 0
    points: tuple | None = None
    float_box: tuple | None = None
    image: str = "owner"


def _rectangle(box, **kwargs):
    x, y, w, h = box
    return ExpectedRoi("rectangle", box, _rect(box), points=((x, y), (x + w, y), (x + w, y + h), (x, y + h)), **kwargs)


def _base(case_id):
    if case_id in ("Q07", "Q08"):
        return None
    if case_id == "Q15":
        return ExpectedRoi("line", (4, 4, 11, 9), LINE_DRAW, points=((4, 4), (14, 12)), float_box=(4, 4, 10, 8))
    if case_id == "Q16":
        return ExpectedRoi("point", (8, 7, 1, 1), frozenset(((8, 7),)), points=((8, 7),), float_box=(8, 7, 0, 0))
    return _rectangle(BASE_BOX)


def _completed(case_id, candidate):
    if case_id == "Q16":
        return None
    if case_id == "Q11":
        return _rectangle(BASE_BOX)
    mode = LATCHED_MODE.get(case_id, 0)
    if not mode:
        return candidate
    base = LINE_AREA if case_id == "Q15" else BASE_PIXELS
    pixels = base | candidate.pixels if mode == 1 else base - candidate.pixels
    kind = "polygon" if case_id == "Q17" else "composite"
    return ExpectedRoi(kind, FINAL_BOXES[case_id], pixels)


def _polygon_stage(label, old):
    points = ((18, 4), (18, 4)) if old is None else old.points
    if label.endswith(".move"):
        point = {"vertex.2.move": (25, 4), "vertex.3.move": (25, 11),
                 "vertex.4.move": (18, 11), "finish.move": (18, 4)}[label]
        points = points[:-1] + (point,)
    if label in ("vertex.2.release", "vertex.3.release", "vertex.4.release"):
        points += (points[-1],)
    if label == "finish.release":
        return ExpectedRoi("polygon", (18, 4, 7, 7), _rect((18, 4, 7, 7)),
                           points=((18, 4), (25, 4), (25, 11), (18, 11)))
    # During construction the reference bounds remain 1x1, float bounds 0x0.
    # A triangle's diagonal excludes the one observed local cell; the later
    # four-sided candidate includes it. No final mask is substituted early.
    pixels = frozenset(((18, 4),)) if (18, 11) in points else frozenset()
    return ExpectedRoi("polygon", (18, 4, 1, 1), pixels, state=0, points=points, float_box=(18, 4, 0, 0))


def _expectations(case_id):
    base, candidate, previous = _base(case_id), None, None
    current, current_ref = base, "base"
    started, canceled, drag = False, False, 0
    out = []
    for event in INPUTS[case_id]["events"]:
        label, kind = event["label"], event["kind"]
        if kind == "setup":
            base, candidate, previous = _base(case_id), None, None
            current, current_ref, started, drag = base, "base", False, 0
        elif case_id == "Q14" and label.startswith("handle."):
            if label == "handle.press":
                base = replace(base, state=4)
            elif label == "handle.drag.1":
                base = _rectangle((7, 5, 7, 7), state=4)
            else:
                base = replace(base, state=3)
            current = candidate = base
            current_ref = "base_candidate"
        elif kind == "key" and label == "escape.down":
            canceled = True
            candidate = replace(candidate, state=3, image="none")
            current, current_ref = replace(_base(case_id), mode=1), "restored"
        elif kind == "mouse":
            action = event["action"]
            if not started and action == "press":
                started = True
                mode = LATCHED_MODE.get(case_id, 0)
                if base is not None:
                    base = replace(base, mode=mode)
                    previous = replace(_base(case_id), mode=mode)
                point = tuple(event["image_point"])
                if case_id == "Q12":
                    candidate = _polygon_stage(label, None)
                elif case_id == "Q13":
                    candidate = ExpectedRoi("freehand", (8, 7, 1, 1), frozenset(), state=0,
                                            points=((8, 7),), float_box=(8, 7, 0, 0))
                elif case_id == "Q09":
                    candidate = ExpectedRoi("ellipse", point + (0, 0), frozenset(), state=0)
                else:
                    candidate = _rectangle(point + (0, 0), state=0)
                current, current_ref = candidate, "candidate"
            elif case_id == "Q12":
                candidate = _polygon_stage(label, candidate)
                current, current_ref = candidate, "candidate"
                if label == "finish.release":
                    current, current_ref = _completed(case_id, candidate), "result"
                    previous = replace(_base(case_id), image="none")
            elif case_id == "Q13" and action == "drag":
                points = candidate.points + (tuple(event["image_point"]),)
                pixels = frozenset(((8, 7),)) if (8, 10) in points else frozenset()
                candidate = replace(candidate, points=points, pixels=pixels)
                current = candidate
            elif action == "drag":
                box = DRAG_BOXES[case_id][drag]
                drag += 1
                candidate = (ExpectedRoi("ellipse", box, _ellipse(box), state=0)
                             if case_id == "Q09" else _rectangle(box, state=0))
                current, current_ref = candidate, "candidate"
            elif action == "release":
                if case_id == "Q13":
                    candidate = replace(candidate, box=HOLE_BOX, float_box=HOLE_BOX,
                                        pixels=HOLE_PIXELS, state=3)
                elif not canceled:
                    candidate = replace(candidate, state=3)
                current = _completed(case_id, candidate)
                current_ref = "candidate" if not LATCHED_MODE.get(case_id, 0) else "result"
                if case_id == "Q16":
                    previous = replace(candidate, image="none")
                elif LATCHED_MODE.get(case_id, 0):
                    previous = replace(_base(case_id), image="none")
        out.append((current, candidate, previous, base, current_ref))
    return out


def _pairs(items):
    value = {}
    for key, item in items:
        assert key not in value, f"Duplicate JSON key: {key}"
        value[key] = item
    return value


def _invalid(value):
    raise AssertionError(f"Nonfinite JSON value: {value}")


def _json(raw):
    return json.loads(raw, object_pairs_hook=_pairs, parse_constant=_invalid)


def _bytes(path, limit=MAX_BYTES):
    assert path.is_file() and not path.is_symlink(), f"Missing regular observation artifact: {path}"
    assert 0 <= path.stat().st_size <= limit
    raw = path.read_bytes()
    assert len(raw) <= limit
    return raw


def _pinned(root, pin, limit=MAX_BYTES):
    assert set(pin) == {"path", "bytes", "sha256"}
    assert type(pin["bytes"]) is int and 0 <= pin["bytes"] <= limit
    path = (root / pin["path"]).resolve()
    assert path.is_relative_to(root) and not Path(pin["path"]).is_absolute()
    raw = _bytes(path, limit)
    assert (len(raw), hashlib.sha256(raw).hexdigest()) == (pin["bytes"], pin["sha256"])
    return raw


def _default_raw_path():
    root = Path(__file__).resolve().parents[2]
    return Path(os.environ.get("IMAGEJXX_SELECTION_CREATION_REFERENCE", str(
        root / "build/references/selection-creation-imagej154r-001/capture-001/reference.stdout"))).resolve()


def _parse_raw(raw):
    assert isinstance(raw, bytes) and len(raw) <= MAX_BYTES
    rows = [_json(line) for line in raw.splitlines() if line.strip()]
    assert tuple(row["id"] for row in rows) == CASE_IDS
    for row in rows:
        _finite(row["elapsed_ms"])
    assert sum(row["elapsed_ms"] for row in rows) <= 120_000
    return dict(zip(CASE_IDS, rows))


def _load_capture(raw_path):
    root = Path(__file__).resolve().parents[2]
    raw_path = Path(raw_path).resolve()
    assert raw_path.is_relative_to(root / "build/references/selection-creation-imagej154r-001")
    receipt = _json(_bytes(raw_path.parent / "capture.json"))
    assert receipt["contract"] == CONTRACT
    assert receipt["state"] == "raw_reference_observations_not_product_or_parity_acceptance"
    assert receipt["ordered_case_ids"] == list(CASE_IDS) and receipt["case_count"] == 17
    raw = _pinned(root, receipt["raw"])
    assert (root / receipt["raw"]["path"]).resolve() == raw_path
    assert receipt["process"]["exit_code"] == 0 and receipt["process"]["timeout_seconds"] == 120
    assert receipt["process"]["stdout"] == receipt["raw"]
    assert _pinned(root, receipt["process"]["stderr"]) == b""
    assert receipt["commercial_eligibility"] == "not_decided_by_this_tool"
    prepared = _json(_pinned(root, receipt["prepared"]))
    assert prepared["contract"] == CONTRACT
    assert prepared["state"] == "prepared_environment_probe_only_full17_capture_not_executed"
    assert receipt["harness"] == prepared["harness"]
    _pinned(root, prepared["harness"])
    plan = _json(_pinned(root, prepared["input_plan"]))
    assert receipt["input_plan"] == prepared["input_plan"]
    assert plan["ordered_case_ids"] == list(CASE_IDS) and plan["cases"] == list(INPUTS.values())
    assert plan["bounds"] == {"canvas_wh": [32, 24], "source_shape": [2, 24, 32], "canvases": 1,
                              "case_seconds": 5, "capture_parent_seconds": 120, "probe_seconds": 20,
                              "compile_seconds": 60, "java_heap_mib": 256,
                              "output_bytes": MAX_BYTES, "build_bytes": 128 * 1024**2}
    provenance = prepared["inputs"]
    old_raw = _pinned(root, provenance["reused_class_receipt"])
    assert hashlib.sha256(old_raw).hexdigest() == CLASS_RECEIPT_SHA
    old = _json(old_raw)
    assert old["reference"] == {"version": "1.54r", "commit": COMMIT,
                                "source_archive_sha256": ARCHIVE_SHA, "runtime_archive_sha256": RUNTIME_SHA}
    assert provenance["source_tree"] == old["verified_source_tree"]
    assert provenance["runtime_tree"] == old["verified_runtime_tree"]
    assert provenance["reused_classes"] == old["compiled_classes"]
    for member, sha in SOURCE_PINS.items():
        assert provenance["source_tree"][member]["sha256"] == sha
    return _parse_raw(raw)


@pytest.fixture(scope="module")
def reference_cases():
    return _load_capture(_default_raw_path())


ROI_KEYS = {"identity", "class", "type", "type_name", "state", "mod_state", "image_id", "is_area", "is_line",
            "bounds_xywh", "float_bounds_xywh", "float_polygon_xy", "shape_array", "stroke_width", "name", "position",
            "czt_position", "mask_observation_reference", "mask_method", "local_mask", "local_mask_error", "mask_rows",
            "mask_pixels_hex", "mask_error"}
KINDS = {"rectangle": ("ij.gui.Roi", 0, "Rectangle"), "ellipse": ("ij.gui.OvalRoi", 1, "Oval"),
         "polygon": ("ij.gui.PolygonRoi", 2, "Polygon"), "freehand": ("ij.gui.FreehandRoi", 3, "Freehand"),
         "composite": ("ij.gui.ShapeRoi", 9, "Composite"), "line": ("ij.gui.Line", 5, "Straight Line"),
         "point": ("ij.gui.PointRoi", 10, "Point")}


def _finite(value):
    assert type(value) in (int, float) and math.isfinite(value)


def _mask_bytes(pixels, box=(0, 0, WIDTH, HEIGHT)):
    x, y, w, h = box
    return bytes(255 if (xx, yy) in pixels else 0 for yy in range(y, y + h) for xx in range(x, x + w))


def _assert_roi(actual, expected, image_id):
    if expected is None:
        assert actual is None
        return
    assert set(actual) == ROI_KEYS
    for key in ("identity", "type", "state", "mod_state", "position"):
        assert type(actual[key]) is int
    assert all(type(value) is int for value in actual["bounds_xywh"] + actual["czt_position"])
    assert (actual["class"], actual["type"], actual["type_name"]) == KINDS[expected.kind]
    assert actual["state"] == expected.state and actual["mod_state"] == expected.mode
    assert actual["image_id"] == (image_id if expected.image == "owner" else None)
    assert actual["is_area"] is (expected.kind not in ("line", "point"))
    assert actual["is_line"] is (expected.kind == "line")
    assert actual["bounds_xywh"] == list(expected.box)
    assert actual["float_bounds_xywh"] == list(expected.float_box or expected.box)
    points = actual["float_polygon_xy"]
    assert isinstance(points, list) and len(points) <= 1024
    for point in points:
        assert isinstance(point, list) and len(point) == 2
        for value in point:
            _finite(value)
    if expected.points is not None:
        assert points == [list(point) for point in expected.points]
    if expected.kind == "composite":
        assert isinstance(actual["shape_array"], list) and 1 <= len(actual["shape_array"]) <= 1024
        for value in actual["shape_array"]:
            _finite(value)
    else:
        assert actual["shape_array"] is None
    # Roi's constructors install the configured default BasicStroke(1) with
    # usingDefaultStroke=true. getStrokeWidth() deliberately returns 0 for that
    # implicit/default stroke; it returns a positive width only for an explicit
    # instance stroke. clone() preserves the default flag, and copyAttributes()
    # passes 0 to setStrokeWidth(), which leaves an existing default untouched.
    # None of these fixtures applies an explicit stroke to an observed ROI.
    assert actual["stroke_width"] == 0 and actual["name"] is None
    assert actual["position"] == 0 and actual["czt_position"] == [0, 0, 0]
    assert actual["mask_observation_reference"] == "clone_of_actual_roi"
    assert actual["mask_method"] == "ByteProcessor.fill(Roi)"
    assert actual["local_mask_error"] is None and actual["mask_error"] is None
    wanted = _mask_bytes(expected.pixels)
    assert actual["mask_pixels_hex"] == wanted.hex()
    assert actual["mask_rows"] == ["".join("1" if wanted[y * WIDTH + x] else "0" for x in range(WIDTH)) for y in range(HEIGHT)]
    if expected.kind in ("rectangle", "line"):
        assert actual["local_mask"] is None
    else:
        local = actual["local_mask"]
        assert set(local) == {"shape", "pixels_hex"}
        assert local["shape"] == [expected.box[3], expected.box[2]]
        assert local["pixels_hex"] == _mask_bytes(expected.pixels, expected.box).hex()


def _assert_snapshot(actual, expected, image_id, paint_count, case_id, keys):
    assert set(actual) == {"current", "candidate", "previous", "base", "relations", "keys", "tool", "image", "canvas",
                           "imagej_visible", "window_visible", "window_showing"}
    for role, wanted in zip(("current", "candidate", "previous", "base"), expected[:4]):
        _assert_roi(actual[role], wanted, image_id)
    assert actual["image"] == {"id": image_id, "slice_index": 1, "stack_size": 2}
    tool = INPUTS[case_id]["tool"]
    assert actual["tool"] == {"id": {"rectangle": 0, "ellipse": 1, "polygon": 2, "freehand": 3}[tool],
                              "name": "oval" if tool == "ellipse" else tool}
    canvas = actual["canvas"]
    assert set(canvas) == {"magnification", "source_rect_xywh", "drawing_wh", "visible", "showing", "displayable", "paint_count"}
    assert canvas["magnification"] == 8 and canvas["source_rect_xywh"] == [0, 0, 32, 24]
    assert canvas["drawing_wh"] == [256, 192] and canvas["paint_count"] == paint_count
    for flag in ("visible", "showing", "displayable"):
        assert type(canvas[flag]) is bool
    # ImageCanvas's child flags do not establish top-level OS presentation.
    assert actual["imagej_visible"] is False and actual["window_visible"] is False and actual["window_showing"] is False
    assert actual["keys"] == keys
    identities = {role: value["identity"] if value is not None else None
                  for role, value in ((key, actual[key]) for key in ("current", "candidate", "previous", "base"))}
    relations = {"current_is_candidate": ("current", "candidate"), "current_is_base": ("current", "base"),
                 "previous_is_base": ("previous", "base"), "current_is_previous": ("current", "previous")}
    assert actual["relations"] == {key: identities[a] is not None and identities[a] == identities[b]
                                    for key, (a, b) in relations.items()}
    current_ref = expected[4]
    if current_ref in ("candidate", "base_candidate"):
        assert actual["relations"]["current_is_candidate"] is True
    elif actual["candidate"] is not None:
        assert actual["relations"]["current_is_candidate"] is False
    assert actual["relations"]["current_is_base"] is (current_ref in ("base", "base_candidate") and actual["base"] is not None)
    assert actual["relations"]["previous_is_base"] is False
    assert actual["relations"]["current_is_previous"] is False


def _assert_delivery(observed, event):
    if event["kind"] == "setup":
        assert observed == {"delivery": "explicit_api_baseline_reset_not_gesture"}
        return
    modifiers = sum({"shift": 64, "ctrl": 128, "alt": 512}[name] for name in event["modifiers"])
    legacy = sum({"shift": 1, "ctrl": 2, "alt": 8}[name] for name in event["modifiers"])
    assert type(observed["when_ms"]) is int and observed["when_ms"] > 0
    assert observed["consumed"] is False
    if event["kind"] == "key":
        assert set(observed) == {"delivery", "id", "when_ms", "key_code", "modifiers", "modifiers_ex", "listeners", "consumed"}
        assert observed["delivery"] == "registered_ImageJ_KeyListener_on_EDT_not_OS_focus_dispatch"
        assert observed["id"] == (401 if event["down"] else 402)
        assert observed["key_code"] == {"shift": 16, "ctrl": 17, "alt": 18, "escape": 27}[event["name"]]
        assert observed["modifiers_ex"] == modifiers and observed["modifiers"] == legacy
        assert observed["listeners"] == ["ij.ImageJ"]
    else:
        assert set(observed) == {"delivery", "id", "when_ms", "screen_point", "offscreen_point", "button", "click_count",
                                 "modifiers", "modifiers_ex", "consumed"}
        action = event["action"]
        assert observed["delivery"] == "ImageCanvas.dispatchEvent_on_EDT"
        assert observed["id"] == {"press": 501, "release": 502, "move": 503, "drag": 506}[action]
        assert observed["screen_point"] == [coordinate * 8 for coordinate in event["image_point"]]
        assert observed["offscreen_point"] == event["image_point"]
        assert observed["button"] == (0 if action in ("move", "drag") else 1)
        assert observed["click_count"] == 1
        assert observed["modifiers_ex"] == modifiers + (1024 if action in ("press", "drag") else 0)
        assert observed["modifiers"] == legacy + (16 if action != "move" else 0)


def _environment(value):
    assert set(value) == {"imagej_version", "java_version", "java_vendor", "os_name", "os_arch", "awt_headless",
                          "event_dispatch_thread", "gui_scale", "canvas_count", "delivery_scope", "pixel_recipe",
                          "brush_size", "line_width", "corner_diameter"}
    assert value["imagej_version"] == "1.54r" and value["java_version"] == "21.0.7"
    assert value["java_vendor"] == "Azul Systems, Inc."
    assert value["os_name"].startswith("Windows") and value["os_arch"] == "amd64"
    assert value["awt_headless"] is False and value["event_dispatch_thread"] is True
    assert (value["gui_scale"], value["canvas_count"], value["brush_size"], value["line_width"], value["corner_diameter"]) == (1, 1, 0, 1, 0)
    assert value["delivery_scope"] == "hidden_NO_SHOW_AWT_handlers_and_offscreen_ImageCanvas.paint_not_OS_UI"
    assert value["pixel_recipe"] == "17*z+3*y+x"


def assert_case(row):
    """Validate one actual case against the independent frozen expectations."""
    case_id = row["id"]
    assert case_id in CASE_IDS
    assert set(row) == {"contract", "id", "input_contract", "initial", "events", "final", "source_before", "source_after", "elapsed_ms", "environment"}
    assert row["contract"] == CONTRACT and row["id"] == case_id
    assert row["input_contract"] == INPUTS[case_id]
    assert len(row["events"]) == EVENT_COUNTS[CASE_IDS.index(case_id)]
    _finite(row["elapsed_ms"])
    assert 0 < row["elapsed_ms"] <= 5000
    _environment(row["environment"])
    pixels = bytes(17 * z + 3 * y + x for z in range(2) for y in range(24) for x in range(32))
    source = {"shape": [2, 24, 32], "dtype": "uint8", "pixels_hex": pixels.hex()}
    assert row["source_before"] == source and row["source_after"] == source
    image_id = row["initial"]["image"]["id"]
    assert type(image_id) is int
    paint_count = row["initial"]["canvas"]["paint_count"]
    assert type(paint_count) is int and paint_count >= 1
    keys = dict.fromkeys(("shift", "alt", "ctrl", "escape"), False)
    base = _base(case_id)
    _assert_snapshot(row["initial"], (base, None, None, base, "base"), image_id, paint_count, case_id, keys)
    expected = _expectations(case_id)
    prior_time = 0
    candidate_id = None
    base_id = row["initial"]["base"]["identity"] if base else None
    for ordinal, (event, actual, wanted) in enumerate(zip(INPUTS[case_id]["events"], row["events"], expected)):
        assert set(actual) == {"ordinal", "label", "input", "delivered", "after"}
        assert actual["ordinal"] == ordinal and actual["label"] == event["label"] and actual["input"] == event
        _assert_delivery(actual["delivered"], event)
        if event["kind"] == "setup":
            keys = dict.fromkeys(keys, False)
            candidate_id = None
            base_id = actual["after"]["base"]["identity"]
        elif event["kind"] == "key":
            # IJ.setKeyUp does not clear its sticky escapePressed flag.
            if event["name"] != "escape" or event["down"]:
                keys[event["name"]] = event["down"]
        if event["kind"] != "setup":
            assert actual["delivered"]["when_ms"] >= prior_time
            prior_time = actual["delivered"]["when_ms"]
        paint_count += 1
        snapshot = actual["after"]
        _assert_snapshot(snapshot, wanted, image_id, paint_count, case_id, keys)
        if snapshot["base"] is not None:
            assert snapshot["base"]["identity"] == base_id
        if snapshot["candidate"] is not None:
            if candidate_id is None:
                candidate_id = snapshot["candidate"]["identity"]
            assert snapshot["candidate"]["identity"] == candidate_id
    assert row["final"] == row["events"][-1]["after"]


def assert_raw(raw):
    """Validate exact17 JSONL bytes or a raw-file path; execute no reference code.

    This checks observations. Use assert_capture as the acceptance entry to also
    verify the capture/preparation/source/runtime receipts and process bounds.
    """
    rows = _parse_raw(raw if isinstance(raw, bytes) else _bytes(Path(raw)))
    for row in rows.values():
        assert_case(row)
    return rows


def assert_capture(raw_path=None):
    """Validate a source-bound captured raw file and all17 independent cases."""
    rows = _load_capture(_default_raw_path() if raw_path is None else raw_path)
    for row in rows.values():
        assert_case(row)
    return rows


@pytest.mark.parametrize("case_id", CASE_IDS)
def test_selection_creation_reference(case_id, reference_cases):
    assert_case(reference_cases[case_id])

