"""Independent ImageJ 1.54r R01--R18 reference-entry assertions.

This reader imports neither the capture producer nor imagejxx. Inputs, scalar
mapping, LUTs and transition rules below are test-owned/source-derived. It keeps
the reference's pixel-zero cache defect and Channels macro outcomes visible;
passing this reader grants no product parity or V01 policy decision.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import struct

import pytest


CONTRACT = "p06c2.composite-max-reference.001"
CASE_IDS = tuple(f"R{i:02d}" for i in range(1, 19))
EXPECTED_COLLECTION_COUNT = 18
EXPECTED_COLLECTION_IDS = tuple(f"test_composite_max_reference[{case}]" for case in CASE_IDS)
STAGE_COUNTS = (3, 3, 3, 3, 7, 6, 3, 3, 3, 3, 6, 4, 4, 4, 4, 4, 4, 7)
RGB_COUNTS = (3, 3, 3, 3, 6, 5, 2, 2, 3, 2, 6, 3, 3, 3, 3, 3, 3, 6)
MAX_PAYLOAD, MAX_BUILD = 2 * 1024**2, 32 * 1024**2
ROOT = Path(__file__).resolve().parents[2]
STAGE = ROOT / "build/references/composite-max-imagej154r-001"
REUSED_CLASSES = ROOT / "build/p06c-001/capture-001/classes"
PRODUCER_SHA = "210cb868a858857a37a579fc69674eeaab2bc03b7780cc4e1086d18c788b1ef0"
COMMIT = "62db90a269cccc1ac79f658e4602043214ed055b"
ARCHIVE_SHA = "09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665"
RUNTIME_SHA = "8e33a11c7e79eb02c055bcee40de806d16e59f0b42e58c4689bd23eb47d990bb"
RECEIPT_SHA = "5360efa8a9f605d845aec107d6d9e1ee940e9732d0bdafdbd07a4c213d1bb471"
MACRO_SHA = "266c77ef8ebae429260b4aca090e43854953630b637e870cb81a71521ad237cb"
SOURCE_PINS = {
    "ij/IJ.java": "8c620842fedd63392268b4fee191e4f6e9ef749ce96bf14a3e9f936e7cea06b4",
    "ij/ImageJ.java": "e043a86f66cbb32c2ab2866502b0e37436b639171697ee706bcab92219d26a59",
    "ij/ImagePlus.java": "477e3f5f913b9c42feb68ea35d21836d45fd56920cccbfc5719a06822cbcfab9",
    "ij/WindowManager.java": "68dc7efb78b8c7fc446b5649a61ad8787366af6afc38f8fe62ae36ca5dddc2bf",
    "ij/gui/StackWindow.java": "333063c0a94206e656c1a67d72e7482397336644c911fc659f73539067146b57",
    "ij/gui/ImageWindow.java": "ca2cf35410411d8b53a02747ad2a5314598b9e79d201c9e632fef4b7aef5810b",
    "ij/gui/ImageCanvas.java": "ab27a57aa060ca8e297d59b09e478406ebd7a3bbd5d6a741bc1ab56c7cc553ab",
    "ij/CompositeImage.java": "7f97ef93b52967cfadda37fb8c26513de283787a93a918d3e54260e43e432a7f",
    "ij/plugin/frame/Channels.java": "172f378b8ece0839559f7c291dfcf98c6c9ec619f4055d3303282ba92ffd1ccc",
    "ij/plugin/Macro_Runner.java": "b9598bffa5b1bbbe851cbafdc31e43680176214020dbb53d6eea3d0665e9346b",
    "ij/process/ImageProcessor.java": "f98e87d25eb6b1f2c5d8557b73dd33e36fef0494d903d6d250206371dccc279c",
    "ij/process/ByteProcessor.java": "fc23c95778bd7154eba1ea56d9ea943e7d1d9ea568fe20cf9bf9c6db1db3ad94",
    "ij/process/ShortProcessor.java": "7484082e91c7095a3c79984c74b4dafa8b6d2912ed3b862e28dd331c8ba3024d",
    "ij/process/FloatProcessor.java": "0f52a157fe99e9a48ce2c9356fc2c3c435dc74f48e45a575171e60deb694ccf5",
    "ij/process/LUT.java": "c3e13b697521012d9bf132efd663c5b73350a09a2be07c95a48c4c105d5631ff",
    "ij/measure/Calibration.java": "2d44c799739c71b1fcdfcf6a5b42a86b65c46e4f6dca9b2cd29b6a479c67f07a",
    "macros/InvertAllLuts.txt": MACRO_SHA,
}
CHOICES = ["Composite", "Color", "Grayscale", "---------", "Composite Max", "Composite Min", "Composite Invert"]
CALIBRATION = {"spacing_xyz": [.5, 1.25, 2.5], "origin_xyz": [11, -7, 19],
               "unit": "um", "frame_interval": .75, "time_unit": "s"}
# Calibration.sanitizeUnit maps the requested ASCII unit to U+00B5 (1.54r,
# Calibration.java:128--135). The observed value must retain that code point.
OBSERVED_CALIBRATION = {**CALIBRATION, "unit": "\u00b5m"}
DEFAULT_TABLES = ("red", "green", "blue", "white", "cyan", "magenta", "yellow", "white")
VALUES = ((0, 31, 127, 192, 255), (255, 64, 127, 128, 0), (13, 250, 65, 128, 201))


def _table(name):
    colors = {"red": (1, 0, 0), "yellow": (1, 1, 0), "cyan": (0, 1, 1),
              "green": (0, 1, 0), "blue": (0, 0, 1), "white": (1, 1, 1), "magenta": (1, 0, 1)}
    if name == "gray":
        return tuple((i, i, i) for i in range(256))
    if name.startswith("inverted_"):
        color = colors[name[9:]]
        return tuple(tuple(255-i*v for v in color) for i in range(256))
    if name in colors:
        return tuple(tuple(i*v for v in colors[name]) for i in range(256))
    if name == "custom_a":
        return tuple(((3*i+7) % 256, (5*i+11) % 256, (7*i+13) % 256) for i in range(256))
    if name == "custom_b":
        return tuple(((11*i+23) % 256, (2*i+101) % 256, (13*i+3) % 256) for i in range(256))
    if name == "custom_c":
        return tuple(((i*i+41) % 256, (17*i+5) % 256, 255-i) for i in range(256))
    if name == "constant":
        return ((37, 91, 143),) * 256
    assert name == "last_step_only"
    return tuple((200-i//2, 160-i//3, 120-i//4) for i in range(255)) + ((250, 240, 230),)


def _table_hex(table):
    return bytes(value for row in table for value in row).hex()


def _fixture(name="main", dtype="uint8", *, values=VALUES, channels=3, width=5, height=1,
             times=1, slices=1, position=(0, 1, 0), ranges=None, tables=None,
             mode="composite", projection=None, flags=None, formula=None, axes=None):
    tables = tables or ("red", "yellow", "cyan")
    ranges = ranges or [[0, 255] for _ in range(channels)]
    planes = []
    for t in range(times):
        for z in range(slices):
            for c in range(channels):
                samples = (tuple(formula(t, c, z, y, x) for y in range(height) for x in range(width))
                           if formula else values[c])
                fmt = {"uint8": "B", "uint16": "H", "float32": "I"}[dtype]
                planes.append({"t": t, "c": c, "z": z,
                               "bits_hex": b"".join(struct.pack("<"+fmt, sample) for sample in samples).hex()})
    return {"name": name, "dtype": dtype, "shape_tczyx": [times, channels, slices, height, width],
            "position_tcz": list(position), "mode": mode, "projection": projection,
            "visibility": list(flags) if flags is not None else [True]*channels,
            "channels": [{"requested_range": list(interval), "table_name": table,
                          "rgb_interleaved_hex": None if table == "reference_default" else _table_hex(_table(table))}
                         for interval, table in zip(ranges, tables)],
            "planes": planes, "bits_encoding": "little_endian_scalar_bits",
            "active_processor_setup": "retain_reference_gray_processor" if channels == 1 else "setChannelLut(current_selected_channel)",
            "construction_axes": axes or {"rows": "Y", "columns": "X"}, "calibration": copy.deepcopy(CALIBRATION)}


def _action(image, label, operation="observe", render=True, **values):
    return {"image": image, "label": label, "operation": operation, "render": render, **values}


def _inputs():
    rows = {}
    for i in (1, 2, 3, 4):
        kwargs = {}
        if i == 2:
            kwargs = {"dtype": "uint16", "values": ((0, 1, 5, 10, 11), (2, 6, 8, 11, 15), (0, 7, 100, 255, 511)),
                      "ranges": ((.2, 10.8), (.6, 10.4), (7.25, 255.75))}
        elif i == 3:
            bits = (0, 0x3DCCCCCC, 0x3DCCCCCD, 0x3DCCCCCE, 0x3E4CCCCD, 0x3E99999A,
                    0x3F000000, 0x3F333333, 0x3F666665, 0x3F666666, 0x3F666667, 0x3F800000)
            kwargs = {"dtype": "float32", "width": 12, "values": (bits, bits[::-1], bits[4:]+bits[:4]), "ranges": [[.1, .9]]*3}
        elif i == 4:
            kwargs = {"tables": ("custom_a", "custom_b", "custom_c")}
        rows[f"R{i:02d}"] = ([_fixture(**kwargs)], [_action("main", "sum"), _action("main", "max", "projection", value="Max"), _action("main", "max.redraw")])
    rows["R05"] = ([_fixture("cold", projection="Max", flags=(False, True, True)), _fixture("warm")],
        [_action("cold", "cold.before", render=False), _action("cold", "cold.max"), _action("cold", "cold.max.redraw"),
         _action("warm", "warm.sum"), _action("warm", "warm.hidden.sum", "visibility", value=[False, True, True]),
         _action("warm", "warm.max", "projection", value="Max"), _action("warm", "warm.max.redraw")])
    rows["R06"] = ([_fixture("cold", projection="Max", flags=(False,)*3), _fixture("warm", projection="Max")],
        [_action("cold", "cold.before", render=False), _action("cold", "cold.empty"), _action("cold", "cold.empty.redraw"),
         _action("warm", "warm.populated"), _action("warm", "warm.empty", "visibility", value=[False]*3), _action("warm", "warm.empty.redraw")])
    rows["R07"] = ([_fixture(channels=1, values=(VALUES[0],), tables=("custom_a",), position=(0, 0, 0), projection="Max")],
        [_action("main", "singleton.before", render=False), _action("main", "singleton.max"), _action("main", "singleton.redraw")])
    rows["R08"] = ([_fixture(channels=8, tables=("reference_default",)*8, position=(0, 6, 0), projection="Max",
        flags=(True, False, True, True, False, True, True, True), values=tuple(tuple((c*29+x*47+11) % 256 for x in range(5)) for c in range(8)))],
        [_action("main", "eight.before", render=False), _action("main", "eight.max"), _action("main", "eight.redraw")])
    rows["R09"] = ([_fixture(dtype="uint16", channels=9, tables=("reference_default",)*9, ranges=[[0, 511]]*9,
        position=(0, 8, 0), values=tuple(tuple((c*31+x*97) % 512 for x in range(5)) for c in range(9)))],
        [_action("main", "nine.color"), _action("main", "nine.max_request", "projection_mode", value="Max", mode="composite"), _action("main", "nine.redraw")])
    quiet = (0x7FC12345, 0x80000000, 0, 0xBF800000, 0x3F000000, 0x40000000)
    rows["R10"] = ([_fixture(dtype="float32", width=6, values=(quiet, quiet[::-1], quiet[2:]+quiet[:2]),
        ranges=[[0, 1]]*3, tables=("custom_a", "custom_b", "custom_c"), projection="Max")],
        [_action("main", "quiet.before", render=False), _action("main", "quiet.max"), _action("main", "quiet.redraw")])
    scalar = lambda t, c, z, y, x: 40*t+30*c+5*z+2*y+x
    rows["R11"] = ([_fixture("xy", "uint16", width=5, height=3, times=2, slices=4, position=(1, 1, 2), formula=scalar, projection="Max"),
        _fixture("xz", "uint16", width=5, height=4, formula=lambda t,c,z,y,x: scalar(1,c,y,1,x), projection="Max",
                 axes={"rows":"Z","columns":"X","fixed_t":1,"fixed_y":1}),
        _fixture("yz", "uint16", width=3, height=4, formula=lambda t,c,z,y,x: scalar(1,c,y,x,1), projection="Max",
                 axes={"rows":"Z","columns":"Y","fixed_t":1,"fixed_x":1})],
        [_action(name, name+suffix) for name in ("xy", "xz", "yz") for suffix in (".max", ".redraw")])
    for i in range(12, 19):
        options = dict(width=5, height=3, times=2, slices=4, position=(1, 1, 2), formula=scalar,
                       ranges=((1, 201), (7, 219), (11, 241)), flags=(True, False, True))
        if i == 13:
            options.update(mode="color", projection="Max", flags=(False, True, False))
        elif i == 14:
            options.update(mode="grayscale", projection="Max", tables=("inverted_red", "inverted_yellow", "inverted_cyan"))
        elif i == 15:
            options.update(position=(1, 2, 2), tables=("inverted_red", "inverted_yellow", "inverted_cyan"))
        elif i == 16:
            options.update(tables=("red", "inverted_yellow", "custom_c"))
        elif i == 17:
            options.update(tables=("red", "constant", "custom_b"))
        elif i == 18:
            options.update(tables=("red", "last_step_only", "custom_a"))
        actions = [_action("main", "setup.warmed"), _action("main", "before", "visibility", render=False, value=list(options["flags"])),
                   _action("main", "choice.max", "channels_choice", render=False, index=4), _action("main", "after.redraw")]
        if i == 18:
            actions += [_action("main", "choice.max.repeated", "channels_choice", render=False, index=4),
                        _action("main", "after.repeated.redraw"), _action("main", "after.repeated.redraw2")]
        rows[f"R{i:02d}"] = ([_fixture(**options)], actions)
    return rows


INPUTS = _inputs()


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, f"Duplicate JSON field {key}"
        result[key] = value
    return result


def _json(raw):
    def invalid(value):
        raise AssertionError(f"Nonfinite JSON token {value}")
    return json.loads(raw, object_pairs_hook=_pairs, parse_constant=invalid)


def _bytes(path, limit=MAX_PAYLOAD):
    assert path.is_file() and not path.is_symlink(), path
    assert 0 <= path.stat().st_size <= limit
    raw = path.read_bytes()
    assert len(raw) <= limit
    return raw


def _pin(pin, limit=MAX_PAYLOAD):
    assert set(pin) == {"path", "bytes", "sha256"}
    assert type(pin["bytes"]) is int and 0 <= pin["bytes"] <= limit
    relative = PurePosixPath(pin["path"])
    assert not relative.is_absolute() and relative.as_posix() == pin["path"]
    assert "\\" not in pin["path"] and ":" not in pin["path"] and not any(part in {"", ".", ".."} for part in relative.parts)
    path = (ROOT / pin["path"]).resolve()
    assert path.is_relative_to(ROOT)
    raw = _bytes(path, limit)
    assert len(raw) == pin["bytes"] and hashlib.sha256(raw).hexdigest() == pin["sha256"]
    return raw


def _finite(value, upper=None):
    assert type(value) in (int, float) and math.isfinite(value) and value >= 0
    if upper is not None:
        assert value <= upper


def _manifest(values):
    return {"members": len(values), "bytes": sum(v["bytes"] for v in values.values()),
            "canonical_sha256": hashlib.sha256(json.dumps(values, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()}


def _default_raw():
    return Path(os.environ.get("IMAGEJXX_COMPOSITE_MAX_REFERENCE", STAGE / "capture-002/reference.stdout")).resolve()


def _environment(value):
    assert value["imagej_version"] == "1.54r" and value["java_version"] == "21.0.7"
    assert value["java_vendor"] == "Azul Systems, Inc." and value["os_name"].startswith("Windows")
    assert value["awt_headless"] is False and value["event_dispatch_thread"] is True
    assert value["gui_scale"] == 1 and value["auto_contrast"] is False and value["use_inverting_lut"] is False
    assert value["imagej_visible"] is False and value["imagej_showing"] is False
    assert value["macro_resource"] == {"name": "/macros/InvertAllLuts.txt", "bytes": 2279, "sha256": MACRO_SHA}
    for window in value["windows"]:
        assert window["visible"] is False and window["showing"] is False and type(window["displayable"]) is bool
    assert value["windows"] and value["scope"] == "programmatic CompositeImage R01-R11; hidden StackWindow/ImageCanvas and real Channels Choice R12-R18; offscreen paint, no OS input"
    from urllib.parse import unquote, urlparse
    assert set(value["loaded_reference_classes"]) == {"Channels","CompositeImage","ImageProcessor","ImageCanvas","ImageWindow","StackWindow"}
    for name in value["loaded_reference_classes"]:
        location = unquote(urlparse(value["loaded_reference_classes"][name]).path)
        if location.startswith("/") and len(location) > 2 and location[2] == ":":
            location = location[1:]
        assert Path(location).resolve() == REUSED_CLASSES.resolve()


def _source(spec):
    times, channels, slices, height, width = spec["shape_tczyx"]
    return {"shape_tczyx": spec["shape_tczyx"], "bits_encoding": "little_endian_scalar_bits",
            "bit_depth": {"uint8":8,"uint16":16,"float32":32}[spec["dtype"]],
            "planes": [dict(plane, stack_index=(plane["t"]*slices+plane["z"])*channels+plane["c"]+1) for plane in spec["planes"]]}


def _inverted(table):
    # ImageProcessor.java299--318 deliberately does not test the254->255 step.
    return not any(sum(table[i-1]) < sum(table[i]) for i in range(1, 255))


def _macro(table):
    # InvertAllLuts' inversion then180-degree HSV hue rotation preserves chroma
    # and is exactly this integer translation of each RGB triple. It is NOT
    # entry reversal, channel permutation or component-wise255-rgb.
    return tuple(tuple(component+255-min(row)-max(row) for component in row) for row in table)


def _f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def _index(sample, dtype, interval):
    low, high = interval
    if math.isnan(sample):
        return 0  # Java casts NaN to integer0; source payload remains untouched.
    if dtype == "uint8":
        if sample < low:
            return 0
        if sample > high:
            return 255
        return max(0, min(255, int(256*(sample-math.floor(low+.5))/(high-low))))
    if dtype == "uint16":
        low, high = math.floor(low+.5), math.floor(high+.5)
        return min(255, int(max(0, sample-low)*256/(high-low+1)+.5))
    low, high = _f32(low), _f32(high)
    return min(255, int(max(0., sample-low)*255/(high-low)+.5))


class _Expected:
    def __init__(self, spec):
        self.spec = spec
        self.mode = "color" if spec["shape_tczyx"][1] > 8 else spec["mode"]
        self.projection = spec["projection"]
        self.position = list(spec["position_tcz"])
        self.flags = [True]*8
        self.flags[:min(8, len(spec["visibility"]))] = spec["visibility"][:8]
        self.ranges = [list(c["requested_range"]) for c in spec["channels"]]
        self.tables = [_table(DEFAULT_TABLES[c] if c < 8 else "white") if channel["table_name"] == "reference_default"
                       else _table(channel["table_name"]) for c, channel in enumerate(spec["channels"])]
        self.processor_table = (_table("gray") if self.mode == "grayscale" or spec["shape_tczyx"][1] == 1
                                else self.tables[self.position[1]])
        self.previous_rgb = None

    def choose_max(self):
        old_mode = self.mode
        self.projection = "Max"
        if _inverted(self.processor_table):
            self.tables = [_macro(table) for table in self.tables]
            self.position[1] = 0
            # The real StackWindow updates the image position before setSlice;
            # its last Stack.setChannel(1) installs the new C0 processor LUT.
            self.processor_table = self.tables[0]
            if _inverted(self.processor_table):
                self.projection = "Min"
        if old_mode != "composite":
            self.flags = [True]*8
        self.mode = "composite"

    def render(self):
        spec = self.spec
        _, channels, _, height, width = spec["shape_tczyx"]
        t, selected, z = self.position
        planes = {}
        for plane in spec["planes"]:
            if plane["t"] == t and plane["z"] == z:
                raw = bytes.fromhex(plane["bits_hex"])
                planes[plane["c"]] = struct.unpack("<"+{"uint8":"B","uint16":"H","float32":"f"}[spec["dtype"]]*(width*height), raw)
        if self.mode != "composite":
            self.flags = [i == selected for i in range(8)]
        mapped = [[self.tables[c][_index(sample, spec["dtype"], self.ranges[c])] for sample in planes[c]] for c in range(channels)]
        if channels == 1 or self.mode != "composite":
            table = _table("gray") if channels == 1 or self.mode == "grayscale" else self.tables[selected]
            result = [table[_index(sample, spec["dtype"], self.ranges[selected])] for sample in planes[selected]]
        else:
            # Preserve the actual CompositeImage updateImage index-zero defect:
            # when C0 is hidden, only pixels1..N-1 are initialized, even cold.
            operation = self.projection or "Sum"
            fill = (255,255,255) if operation == "Min" else (0,0,0)
            result = [fill]*(width*height)
            if self.flags[0]:
                result = list(mapped[0])
            elif self.previous_rgb is not None:
                result[0] = self.previous_rgb[0]
            for c in range(1, channels):
                if self.flags[c]:
                    combine = (lambda a,b: max(a,b)) if operation == "Max" else ((lambda a,b: min(a,b)) if operation == "Min" else (lambda a,b: min(255,a+b)))
                    result = [tuple(combine(a,b) for a,b in zip(old,new)) for old,new in zip(result,mapped[c])]
        self.previous_rgb = result
        return bytes(component for pixel in result for component in pixel).hex()


def _state(actual, expected, image_id):
    assert set(actual) == {"image_id","mode","mode_integer","projection","host","position_tcz","channel_count",
                           "active_flags","channel_state","cache","active_processor","calibration"}
    assert actual["image_id"] == image_id and type(image_id) is int
    assert actual["mode"] == expected.mode and actual["mode_integer"] == {"composite":1,"color":2,"grayscale":3}[expected.mode]
    assert actual["projection"] == expected.projection and actual["position_tcz"] == expected.position
    assert actual["channel_count"] == expected.spec["shape_tczyx"][1] and actual["active_flags"] == expected.flags
    assert all(type(flag) is bool for flag in actual["active_flags"])
    assert actual["calibration"] == OBSERVED_CALIBRATION
    assert actual["channel_state"] == [{"channel": c, "effective_range": interval, "rgb_interleaved_hex": _table_hex(table)}
                                        for c, (interval,table) in enumerate(zip(expected.ranges, expected.tables))]
    cache = actual["cache"]
    assert set(cache) == {"rgb_pixels_present","channel_processors_present","image_present","single_channel","current_channel","current_slice","current_frame"}
    for key in ("rgb_pixels_present","channel_processors_present","image_present","single_channel"):
        assert type(cache[key]) is bool
    for key in ("current_channel","current_slice","current_frame"):
        assert type(cache[key]) is int
    assert cache["single_channel"] is False
    processor = actual["active_processor"]
    assert set(processor) == {"class","range","is_inverted_lut","rgb_interleaved_hex"}
    assert processor["class"] == "ij.process."+{"uint8":"ByteProcessor","uint16":"ShortProcessor","float32":"FloatProcessor"}[expected.spec["dtype"]]
    low, high = expected.ranges[expected.position[1]]
    effective = [math.floor(v+.5) if expected.spec["dtype"] != "float32" else _f32(v) for v in (low,high)]
    assert processor["range"] == effective
    raw = bytes.fromhex(processor["rgb_interleaved_hex"])
    assert len(raw) == 768 and type(processor["is_inverted_lut"]) is bool
    table = tuple(tuple(raw[i:i+3]) for i in range(0,768,3))
    assert processor["is_inverted_lut"] == _inverted(table)
    # Entering Composite does not replace a preexisting grayscale processor
    # LUT (CompositeImage.setMode:517--543), even though channel LUTs persist.
    assert table == expected.processor_table


def _host(actual, expected, image_id, *, painted, last_label, identities=None, configured=True, paint_completed=False):
    """Validate actual object/navigation/pixel binding, not a presentation flag."""
    host = actual["host"]
    assert isinstance(host, dict)
    assert set(host) == {"image_id","image_identity","window_identity","canvas_identity","window_class","canvas_class",
        "image_window_identity","window_image_id","window_image_identity","window_canvas_identity","canvas_image_id",
        "canvas_image_identity","canvas_parent_identity","current_window_is_host","current_image_is_source",
        "displayed_hyperstack","open_as_hyperstack","window_hyperstack","valid_dimensions","shape_tczyx","position_tcz",
        "current_slice_index","stack_size","window_position_czt","selector_position_czt","window","canvas",
        "processor_binding","offscreen_paint"}
    for key in ("image_identity","window_identity","canvas_identity"):
        assert type(host[key]) is int and host[key] > 0
    identity = tuple(host[key] for key in ("image_identity","window_identity","canvas_identity"))
    assert len(set(identity)) == 3
    if identities is not None:
        assert identity == identities
    assert host["image_id"] == host["window_image_id"] == host["canvas_image_id"] == image_id
    assert host["image_identity"] == host["window_image_identity"] == host["canvas_image_identity"]
    assert host["window_identity"] == host["image_window_identity"] == host["canvas_parent_identity"]
    assert host["canvas_identity"] == host["window_canvas_identity"]
    assert host["window_class"] == "CompositeMaxReference$HiddenStackWindow" and host["canvas_class"] == "ij.gui.ImageCanvas"
    for key in ("current_window_is_host","current_image_is_source","displayed_hyperstack","open_as_hyperstack","window_hyperstack","valid_dimensions"):
        assert host[key] is True
    shape = expected.spec["shape_tczyx"]
    times, channels, slices, height, width = shape
    t, c, z = expected.position
    index = (t*slices+z)*channels+c
    assert host["shape_tczyx"] == shape and host["position_tcz"] == expected.position
    assert host["window_position_czt"] == host["selector_position_czt"] == [c,z,t]
    assert host["current_slice_index"] == index and host["stack_size"] == times*channels*slices
    window = host["window"]
    assert set(window) == {"visible","showing","displayable"}
    assert window["visible"] is False and window["showing"] is False and type(window["displayable"]) is bool
    canvas = host["canvas"]
    assert set(canvas) == {"visible","showing","displayable","magnification","source_rect_xywh","drawing_wh","image_updated","paint_pending"}
    for key in ("visible","showing","displayable","image_updated","paint_pending"):
        assert type(canvas[key]) is bool
    assert canvas["showing"] is False
    assert canvas["source_rect_xywh"] == [0,0,width,height]
    if configured:
        assert canvas["magnification"] == 8 and canvas["drawing_wh"] == [width*8,height*8]
    else:
        # Window construction sizes its original canvas before fixture view
        # configuration; it is not evidence for a particular screen size.
        assert type(canvas["magnification"]) in (int,float) and math.isfinite(canvas["magnification"]) and canvas["magnification"] > 0
        assert len(canvas["drawing_wh"]) == 2 and all(type(v) is int and v > 0 for v in canvas["drawing_wh"])
    pixels = host["processor_binding"]
    assert set(pixels) == {"identity","pixels_identity","source_pixels_identity","source_stack_index","pixels_are_source",
                          "scalar_bits_hex","source_scalar_bits_hex"}
    for key in ("identity","pixels_identity","source_pixels_identity"):
        assert type(pixels[key]) is int and pixels[key] > 0
    bits = next(plane["bits_hex"] for plane in expected.spec["planes"] if (plane["t"],plane["c"],plane["z"]) == (t,c,z))
    assert pixels["pixels_identity"] == pixels["source_pixels_identity"] and pixels["pixels_are_source"] is True
    assert pixels["source_stack_index"] == index and pixels["scalar_bits_hex"] == pixels["source_scalar_bits_hex"] == bits
    paint = host["offscreen_paint"]
    assert set(paint) == {"method","count","last_label","surface_wh"}
    assert type(paint["count"]) is int and paint["count"] == painted and paint["last_label"] == last_label
    assert paint["method"] == "ImageCanvas.paint(Graphics2D) on private BufferedImage.TYPE_INT_RGB"
    assert paint["surface_wh"] == [width*8,height*8]
    if paint_completed:
        assert canvas["image_updated"] is False and canvas["paint_pending"] is False
    return identity


HOST_ORDER = ["construct_base_CompositeImage","observe_before_attachment","construct_original_ImageCanvas",
    "construct_hidden_StackWindow","bind_current_window_and_temporary_image","observe_after_attachment",
    "apply_fixture_channel_tables_and_ranges","setPosition_through_StackWindow","set_current_channel_LUT",
    "setDisplayRange","set_projection_if_present","setMode","set_visibility","set_canvas_source_rect",
    "set_canvas_drawing_size","set_canvas_magnification_last","observe_configured"]
TRANSITION_HOST = {"case_ids":list(CASE_IDS[11:]), "window_class":"CompositeMaxReference$HiddenStackWindow",
    "canvas_class":"ij.gui.ImageCanvas", "presentation_override":"show/setVisible only", "setup_order":HOST_ORDER,
    "magnification":8, "source_rect":"full fixture XY", "offscreen_surface":"BufferedImage.TYPE_INT_RGB",
    "paint_method":"ImageCanvas.paint(Graphics2D) on private BufferedImage.TYPE_INT_RGB",
    "readback_order":["operation_or_synchronous_Channels_handler","updateImage_if_render_true","ImageCanvas.paint_offscreen",
                      "same_image_RGB_readback","observe_after_state"],
    "handler_observation_order":"state_after_handler_before_offscreen_paint_and_RGB_readback",
    "cleanup":"after source_after/environment observation, actual StackWindow.close with changes=false"}


def _host_setup(setups, fixture):
    assert set(setups) == {fixture["name"]}
    setup = setups[fixture["name"]]
    assert set(setup) == {"order","before_attachment","after_attachment","configured"} and setup["order"] == HOST_ORDER
    initial = copy.deepcopy(fixture)
    initial.update(position_tcz=[0,0,0], mode="composite", projection=None,
                   visibility=[True]*initial["shape_tczyx"][1])
    initial["channels"] = [{"requested_range":[0,255], "table_name":"reference_default", "rgb_interleaved_hex":None}
                           for _ in initial["channels"]]
    expected = _Expected(initial)
    # CompositeImage construction keeps the source ByteProcessor's default
    # grayscale base table; it does not make it the stored red channel LUT.
    expected.processor_table = _table("gray")
    identity = setup["before_attachment"]["image_id"]
    assert setup["before_attachment"]["host"] is None
    _state(setup["before_attachment"], expected, identity)
    _state(setup["after_attachment"], expected, identity)
    objects = _host(setup["after_attachment"], expected, identity, painted=0, last_label=None, configured=False)
    _state(setup["configured"], _Expected(fixture), identity)
    _host(setup["configured"], _Expected(fixture), identity, painted=0, last_label=None, identities=objects)
    return objects


def _choice(value, *, count=None):
    assert value["items"] == CHOICES and value["channels_listener_registered"] is True
    assert value["handler_declaring_class"] == "ij.plugin.frame.Channels"
    assert value["visible"] is False and value["showing"] is False
    assert type(value["identity"]) is int and value["identity"] > 0
    assert type(value["selected_index"]) is int and value["selected_item"] == CHOICES[value["selected_index"]]
    assert value["listeners"] and "CompositeMaxReference$HiddenChannels" in value["listeners"]
    assert type(value["delivered_item_events"]) is int
    if count is not None:
        assert value["delivered_item_events"] == count


def assert_case(row):
    case = row["id"]
    assert case in CASE_IDS and row["contract"] == CONTRACT
    assert set(row) == {"contract","id","input_contract","host_setup","source_before","source_after","stages","environment","elapsed_ms"}
    fixtures, actions = INPUTS[case]
    spec = row["input_contract"]
    assert set(spec) == {"id","driving_method","question","fixtures","actions"}
    assert spec["id"] == case and spec["fixtures"] == fixtures and spec["actions"] == actions
    assert isinstance(spec["question"], str) and spec["question"]
    assert spec["driving_method"] == ("actual_Channels_Choice_ItemEvent" if int(case[1:]) >= 12 else "programmatic_CompositeImage_public_API")
    source = {fixture["name"]: _source(fixture) for fixture in fixtures}
    assert row["source_before"] == row["source_after"] == source
    _finite(row["elapsed_ms"], 5000)
    _environment(row["environment"])
    assert len(row["stages"]) == STAGE_COUNTS[CASE_IDS.index(case)]
    hosted = int(case[1:]) >= 12
    if hosted:
        host_identities = _host_setup(row["host_setup"], fixtures[0])
        assert row["stages"][0]["before"] == row["host_setup"][fixtures[0]["name"]]["configured"]
    else:
        assert row["host_setup"] == {}
    expected = {fixture["name"]: _Expected(fixture) for fixture in fixtures}
    identities, previous, dispatches, rgb_count = {}, {}, 0, 0
    paint_count, paint_label = 0, None
    for stage, action in zip(row["stages"], actions):
        assert set(stage) == {"label","input","before","after","rgb","dispatch","elapsed_ms"}
        assert stage["label"] == action["label"] and stage["input"] == action
        _finite(stage["elapsed_ms"], 5000)
        state = expected[action["image"]]
        identity = identities.setdefault(action["image"], stage["before"]["image_id"])
        if action["image"] in previous:
            assert stage["before"] == previous[action["image"]]
        _state(stage["before"], state, identity)
        if hosted:
            _host(stage["before"], state, identity, painted=paint_count, last_label=paint_label, identities=host_identities)
        else:
            assert stage["before"]["host"] is None
        operation = action["operation"]
        if operation in ("projection", "projection_mode"):
            state.projection = action["value"]
        if operation == "projection_mode":
            state.mode = "color" if state.spec["shape_tczyx"][1] > 8 else action["mode"]
            state.flags = [True]*8
        if operation == "visibility":
            state.flags[:min(8,len(action["value"]))] = action["value"][:8]
        if operation == "channels_choice":
            delivered = stage["dispatch"]
            assert delivered["method"] == "Choice.dispatchEvent(ItemEvent)"
            assert delivered["event_id"] == 701 and delivered["state_change"] == 1
            assert delivered["event_item"] == CHOICES[4] and delivered["requested_index"] == 4
            _choice(delivered["before"])
            prior_choice = ({"color":1,"grayscale":2}.get(state.mode,
                {None:0,"Sum":0,"Max":4,"Min":5,"Invert":6}[state.projection]))
            assert delivered["before"]["selected_index"] == prior_choice
            _choice(delivered["after"], count=delivered["before"]["delivered_item_events"]+1)
            assert delivered["source_identity"] == delivered["before"]["identity"] == delivered["after"]["identity"]
            assert delivered["current_image_id"] == identity
            state.choose_max()
            _state(delivered["state_after_handler_before_readback"], state, identity)
            _host(delivered["state_after_handler_before_readback"], state, identity,
                  painted=paint_count, last_label=paint_label, identities=host_identities)
            dispatches += 1
        else:
            assert stage["dispatch"] is None
        rendered = action["render"] or operation == "channels_choice"
        if rendered:
            wanted = state.render()
            assert stage["rgb"]["shape_yx"] == state.spec["shape_tczyx"][-2:]
            assert stage["rgb"]["method"] in {"BufferedImage.getRGB", "PixelGrabber"}
            assert stage["rgb"]["rgb_interleaved_hex"] == wanted, (case, action["label"])
            rgb_count += 1
            if hosted:
                paint_count += 1
                paint_label = action["label"]
        else:
            assert stage["rgb"] is None
        _state(stage["after"], state, identity)
        if hosted:
            _host(stage["after"], state, identity, painted=paint_count, last_label=paint_label,
                  identities=host_identities, paint_completed=rendered)
        else:
            assert stage["after"]["host"] is None
        previous[action["image"]] = stage["after"]
        cache = stage["after"]["cache"]
        if rendered:
            assert cache["image_present"] is True and cache["current_channel"] == state.position[1]
            assert cache["rgb_pixels_present"] is (state.mode == "composite" and state.spec["shape_tczyx"][1] > 1)
            assert cache["channel_processors_present"] is (state.mode == "composite" and state.spec["shape_tczyx"][1] > 1)
        elif action["label"].endswith(".before"):
            assert cache["rgb_pixels_present"] is False and cache["channel_processors_present"] is False
    assert len(set(identities.values())) == len(fixtures)
    assert rgb_count == RGB_COUNTS[CASE_IDS.index(case)]
    assert dispatches == (2 if case == "R18" else 1 if int(case[1:]) >= 12 else 0)
    assert paint_count == (RGB_COUNTS[CASE_IDS.index(case)] if hosted else 0)


def _raw(raw):
    assert isinstance(raw, bytes) and 0 < len(raw) <= MAX_PAYLOAD
    rows = [_json(line) for line in raw.splitlines() if line.strip()]
    assert [row["id"] for row in rows] == list(CASE_IDS)
    assert sum(len(row["stages"]) for row in rows) == 74
    assert sum(stage["rgb"] is not None for row in rows for stage in row["stages"]) == 62
    assert sum(len(row["source_before"]) for row in rows) == 22
    dispatches = [stage["dispatch"] for row in rows for stage in row["stages"] if stage["dispatch"] is not None]
    assert len(dispatches) == 8
    for count, dispatch in enumerate(dispatches):
        _choice(dispatch["before"], count=count)
        _choice(dispatch["after"], count=count+1)
    assert len({dispatch["source_identity"] for dispatch in dispatches}) == 1
    assert sum(row["elapsed_ms"] for row in rows) <= 45_000
    return dict(zip(CASE_IDS, rows))


def _load(raw_path):
    raw_path = Path(raw_path).resolve()
    assert raw_path.is_relative_to(STAGE.resolve())
    receipt = _json(_bytes(raw_path.parent / "capture.json"))
    assert receipt["contract"] == CONTRACT and receipt["state"] == "raw_reference_observations_only_no_product_or_parity_acceptance"
    assert receipt["ordered_case_ids"] == list(CASE_IDS) and receipt["case_count"] == 18 and receipt["stage_count"] == 74
    _finite(receipt["elapsed_seconds"], 60)
    assert receipt["policy_decisions"] == [] and receipt["commercial_eligibility"] == "not_decided_by_capture_tool"
    raw = _pin(receipt["raw"])
    assert (ROOT / receipt["raw"]["path"]).resolve() == raw_path
    process = receipt["process"]
    assert process["exit_code"] == 0 and 1 <= process["timeout_seconds"] <= 50
    assert process["stdout"] == receipt["raw"] and _pin(process["stderr"]) == b""
    assert "-Xmx256m" in process["argv"] and "-Djava.awt.headless=false" in process["argv"]
    assert process["argv"][-2:] == ["CompositeMaxReference", "--capture"]
    prepared = _json(_pin(receipt["prepared"]))
    assert prepared["contract"] == CONTRACT and prepared["state"] == "prepared_probe_only_R01_R18_not_executed"
    assert receipt["harness"] == prepared["harness"] and prepared["harness"]["sha256"] == PRODUCER_SHA
    _pin(prepared["harness"])
    plan = _json(_pin(prepared["input_plan"]))
    assert plan["contract"] == CONTRACT and plan["ordered_case_ids"] == list(CASE_IDS) and plan["stage_count"] == 74
    assert plan["bounds"] == {"case_ids":18,"case_seconds":5,"capture_parent_seconds":60,"java_case_loop_seconds":45,
        "java_process_seconds":50,"compile_seconds":60,"probe_seconds":20,"java_heap_mib":256,
        "combined_payload_bytes":MAX_PAYLOAD,"fresh_stage_bytes":MAX_BUILD,"max_plane_pixels":20,
        "max_scalar_samples_per_fixture":360,"max_scalar_samples_per_case":456}
    membership = plan["output_membership"]
    assert membership["raw_jsonl_rows"] == 18 and membership["stages"] == 74 and membership["RGB_readbacks"] == 62
    assert membership["source_before_after_pairs"] == 22 and membership["real_Channels_dispatches"] == 8
    assert membership["per_case_stage_counts"] == dict(zip(CASE_IDS, STAGE_COUNTS))
    assert membership["per_case_RGB_counts"] == dict(zip(CASE_IDS, RGB_COUNTS))
    assert plan["environment_inputs"] == {"ImageJ_constructor":"ImageJ.NO_SHOW", "Channels_presentation":"subclass show/setVisible only",
        "gui_scale":1, "auto_contrast":False, "use_inverting_lut":False,
        "current_image_binding":"WindowManager.setCurrentWindow for hosted cases and setTempCurrentImage on actual EDT",
        "transition_input":"actual Choice.dispatchEvent(ItemEvent.SELECTED, index 4)", "transition_image_host":TRANSITION_HOST}
    assert plan["scope"] == "programmatic R01-R11 and real hidden StackWindow/ImageCanvas plus Channels Choice dispatch R12-R18; raw reference only, no product/OS-input/commercial acceptance"
    observed = _raw(raw)
    assert plan["cases"] == [row["input_contract"] for row in observed.values()]
    assert plan["ordered_stage_labels"] == {case:[a["label"] for a in INPUTS[case][1]] for case in CASE_IDS}
    verified = _json(_pin(prepared["verified_inputs"]))
    expected_reference = {"version":"1.54r","commit":COMMIT,"source_archive_sha256":ARCHIVE_SHA,"runtime_archive_sha256":RUNTIME_SHA}
    assert verified["reference"] == expected_reference
    assert [(pin["bytes"], pin["sha256"]) for pin in verified["archives"]] == [(6146010, ARCHIVE_SHA), (705099776, RUNTIME_SHA)]
    old_raw = _pin(verified["reused_receipt"])
    assert hashlib.sha256(old_raw).hexdigest() == RECEIPT_SHA
    old = _json(old_raw)
    assert old["reference"] == expected_reference
    for key, original in (("source_tree","verified_source_tree"),("runtime_tree","verified_runtime_tree"),("reused_classes","compiled_classes")):
        assert verified[key] == _manifest(old[original])
    assert set(verified["known_source_members"]) == set(SOURCE_PINS)
    for name, sha in SOURCE_PINS.items():
        assert verified["known_source_members"][name] == old["verified_source_tree"][name]
        assert verified["known_source_members"][name]["sha256"] == sha
    execution = _json(_pin(prepared["execution_inputs"]))
    assert execution["input_plan"] == prepared["input_plan"] and execution["verified_inputs"] == prepared["verified_inputs"]
    assert execution["driver"] == prepared["driver"]
    _pin(prepared["driver"])
    assert hashlib.sha256(_pin(execution["macro_resource"])).hexdigest() == MACRO_SHA
    assert execution["macro_origin"]["member"] == f"ImageJ-{COMMIT}/macros/InvertAllLuts.txt"
    assert execution["macro_origin"]["member_sha256"] == MACRO_SHA
    assert execution["macro_origin"]["archive"] == verified["archives"][0]
    for tool in execution["tools"]:
        _pin(tool)
    assert set(prepared["prepared_classes"]) == {"CompositeMaxReference.class", "CompositeMaxReference$HiddenChannels.class",
        "CompositeMaxReference$HiddenStackWindow.class", "CompositeMaxReference$Host.class", "macros/InvertAllLuts.txt"}
    for name, pin in prepared["prepared_classes"].items():
        relative = PurePosixPath(name)
        assert relative.as_posix() == name and not relative.is_absolute() and ".." not in relative.parts
        assert name.endswith(".class") or name == "macros/InvertAllLuts.txt"
        path = ROOT / receipt["prepared"]["path"]
        class_path = path.parent / "classes" / name
        content = _bytes(class_path, MAX_BUILD)
        assert len(content) == pin["bytes"] and hashlib.sha256(content).hexdigest() == pin["sha256"]
    assert sum(row["bytes"] for row in prepared["prepared_classes"].values()) <= MAX_BUILD
    _environment(prepared["probe"]["environment"])
    _choice(prepared["probe"]["choice"], count=0)
    assert prepared["probe"]["kind"] == "environment_probe_no_R_dispatch"
    probe_fixture = INPUTS["R12"][0][0]
    probe_ids = _host_setup(prepared["probe"]["host_setup"], probe_fixture)
    probe = prepared["probe"]["state"]
    _state(probe, _Expected(probe_fixture), probe["image_id"])
    _host(probe, _Expected(probe_fixture), probe["image_id"], painted=1, last_label="probe", identities=probe_ids, paint_completed=True)
    for name, timeout in (("compile",60),("version",10),("probe",20)):
        held = prepared["processes"][name]
        assert held["exit_code"] == 0 and held["timeout_seconds"] == timeout
        _pin(held["stdout"]); _pin(held["stderr"])
    assert _json(_pin(prepared["processes"]["probe"]["stdout"])) == prepared["probe"]
    consumed = _json(_pin(receipt["execution_inputs"]))
    assert consumed["prepared"] == receipt["prepared"] and consumed["harness"] == prepared["harness"]
    for name in ("input_plan","verified_inputs","driver"):
        assert consumed[name] == prepared[name]
    for stage in (raw_path.parent, (ROOT / receipt["prepared"]["path"]).parent):
        files = [path for path in stage.rglob("*") if path.is_file()]
        assert all(not path.is_symlink() for path in files)
        assert sum(path.stat().st_size for path in files) <= MAX_BUILD
        assert sum(path.stat().st_size for path in files if path.suffix in {".json", ".stdout", ".stderr"}) <= MAX_PAYLOAD
    return observed


@pytest.fixture(scope="module")
def reference_cases():
    return _load(_default_raw())


def assert_raw(raw):
    rows = _raw(raw if isinstance(raw,bytes) else _bytes(Path(raw)))
    for row in rows.values():
        assert_case(row)
    return rows


def assert_capture(raw_path=None):
    rows = _load(_default_raw() if raw_path is None else raw_path)
    for row in rows.values():
        assert_case(row)
    return rows


@pytest.mark.parametrize("case_id", CASE_IDS)
def test_composite_max_reference(case_id, reference_cases):
    assert_case(reference_cases[case_id])
