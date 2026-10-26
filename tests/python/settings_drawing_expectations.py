"""Independent C3 literals; no product import or production rasterizer reuse.

Raster sample centers are integer source coordinates. Shape coverage below is
enumerated directly, not calculated using the application's distance kernel.
Rounded coverage/curves reuse an unchanged, existing independent reference.
"""
import hashlib
import json
from pathlib import Path
import re
import struct


WIDTH, HEIGHT = 41, 33
FOREGROUND = (224, 31, 64)
BACKGROUND = (16, 32, 224)
ACCEPTED_SETTINGS = {
    "foreground": "#e01f40", "background": "#1020e0", "brush_width": 3.0,
    "rounded_corner_diameter": 5, "line_width": 3.0, "point_size": 5.0,
    "text_size": 23,
}
THIN_SETTINGS = dict(ACCEPTED_SETTINGS, brush_width=1.0, line_width=1.0, point_size=1.0)
ROUNDED_REFERENCE_SHA256 = "c240506730a694f8b0bf2861a8428dcaa11885ee028d69a76d99fb1836d831fa"
ROUNDED_REFERENCE_ID = "R011"
# The unchanged owning native rounded-path contract uses this comparison policy.
ROUNDED_PATH_RELATIVE_TOLERANCE = 2e-12

_SINGLE = frozenset({(4, 4)})
_SQUARE = frozenset((x, y) for y in (3, 4, 5) for x in (3, 4, 5))
_RECTANGLE = frozenset((x, y) for y in (2, 3, 4) for x in (2, 3, 4))
_LINE_THIN = frozenset((x, 4) for x in (2, 3, 4, 5, 6))
_LINE = frozenset((x, y) for y in (3, 4, 5) for x in (1, 2, 3, 4, 5, 6, 7))
_POINT = frozenset(
    [(x, y) for y in (2, 6) for x in (3, 4, 5)] +
    [(x, y) for y in (3, 4, 5) for x in (2, 3, 4, 5, 6)])
LITERAL_COVERAGE = {
    "fill": _RECTANGLE, "clear": _RECTANGLE,
    "thin_brush": _SINGLE, "thin_line": _LINE_THIN, "thin_point": _SINGLE,
    "brush": _SQUARE, "eraser": _SQUARE, "line": _LINE, "point": _POINT,
}


def initial_rgb():
    return tuple(value for y in range(HEIGHT) for x in range(WIDTH)
                 for value in (10 + x, 20 + y, 30 + x + y))


def rounded_reference():
    path = Path(__file__).resolve().parents[1] / "native/rounded_path_reference.inc"
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != ROUNDED_REFERENCE_SHA256:
        raise ValueError("Original independent rounded reference changed or is unavailable")
    rows = re.findall(r'^\s*\{"R011",\{([^}]+)\},5,\{([^}]+)\},"([01]+)"\},?\s*$',
                      raw.decode("utf-8"), re.MULTILINE)
    if len(rows) != 1:
        raise ValueError("Exactly one unchanged rounded R011 row is required")
    frame, curve, mask = rows[0]
    frame = tuple(json.loads("[" + frame + "]"))
    curve = tuple(json.loads("[" + curve + "]"))
    if frame != (14.25, 12.75, 4.832650480625312, 2.051338424568687, -1.4652417318347766, 3.4518932004466514) or len(curve) != 44 or len(mask) != WIDTH * HEIGHT:
        raise ValueError("Rounded reference does not match the frozen C3 fixture")
    return {"id": ROUNDED_REFERENCE_ID, "frame": frame, "diameter": 5,
            "curve": curve, "mask": tuple(int(word) for word in mask)}


def expected_mask(operation):
    if operation == "rounded":
        return rounded_reference()["mask"]
    coverage = LITERAL_COVERAGE[operation]
    return tuple(int((x, y) in coverage) for y in range(HEIGHT) for x in range(WIDTH))


def expected_rgb(operation):
    if operation == "text":
        # Text creates an overlay. Glyph rasterization is a separate font-bound claim.
        return initial_rgb()
    mask = expected_mask(operation)
    color = BACKGROUND if operation in {"clear", "eraser"} else FOREGROUND
    return tuple(value for y in range(HEIGHT) for x in range(WIDTH)
                 for value in (color if mask[y * WIDTH + x] else (10 + x, 20 + y, 30 + x + y)))


def storage_f64le_hex(values):
    """Exact backing words for the declared integral source/edited RGB values."""
    return struct.pack("<" + "d" * len(values), *values).hex()
