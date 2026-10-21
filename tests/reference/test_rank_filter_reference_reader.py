"""Independent fixed-input assertions for excluded ImageJ 1.54r rank capture.

No producer or imagejxx implementation is imported. Original Java observations
are checked against test-owned inputs, circular neighborhoods, ordered floating
arithmetic, runner masking and actual component/preview lineage. This reader
does not grant native/editor/Fiji/commercial or OS-input acceptance.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import struct

import pytest

ROOT = Path(__file__).resolve().parents[2]
STAGE = ROOT/"build/references/rank-filter-imagej154r-001"
CONTRACT = "g042.rank-filter-reference.001"
JAVA_CLASS = "ij.plugin.filter.RankFilterReference"
CASE_IDS = tuple(f"K{i:02d}" for i in range(1, 17))
EXPECTED_COLLECTION_COUNT = 16
EXPECTED_COLLECTION_IDS = tuple(f"test_rank_filter_reference[{case}]" for case in CASE_IDS)
RUN_COUNTS = (0, 3, 3, 3, 3, 8, 1, 1, 108, 8, 12, 4, 12, 8, 0, 0)
MAX_PAYLOAD, MAX_BUILD = 8*1024**2, 32*1024**2
BOUNDS = {"case_count": 16, "max_pixels_per_fixture": 4096, "max_scalar_samples_per_case": 131072,
          "case_seconds": 5, "prepare_parent_seconds": 120, "capture_parent_seconds": 120,
          "java_heap_mib": 256, "combined_payload_bytes": MAX_PAYLOAD,
          "fresh_stage_bytes": MAX_BUILD, "max_live_hosts": 2}
ENTRY_SHA = "7b4e4edd133d34431f8e763fbc8db82d215d4e6e5b546402ea6203fcb43b0b02"
LINEAGE_SHA = "84e355ed4a509c1e95ae63f8736cddf3f23a68c6ce2bad0bbe91e0fb37e8f16a"
PRODUCER_SHA = "31b109277696b0715c8ae8912acf9d51a91c89fe7c2bbb84dcc1e7cd5ea284ee"
SUPERVISOR_SHA = "3c8ff4700ef86cdd930c7ab8ec424d4a207b26a1672d1d1cc49811af5c3a4653"
BINARY_HELPER_SHA = "65ac94eef8b0d165f29cfb1a971a511a07975478de3e92c49de20d5514f301db"
COMMIT = "62db90a269cccc1ac79f658e4602043214ed055b"
ARCHIVE_SHA = "09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665"
RUNTIME_SHA = "8e33a11c7e79eb02c055bcee40de806d16e59f0b42e58c4689bd23eb47d990bb"
RECEIPT_SHA = "5360efa8a9f605d845aec107d6d9e1ee940e9732d0bdafdbd07a4c213d1bb471"
SOURCE_PINS = {
    "ij/plugin/filter/RankFilters.java": "284e7647e6619a3cc62887c57992056ccfd1dd99661868571fff005afff7d6b7",
    "ij/plugin/filter/PlugInFilterRunner.java": "71e9965a1984feafc184b336f1a575f7644e057631ce1d66fdd2deea7c10df4f",
    "ij/process/ImageProcessor.java": "f98e87d25eb6b1f2c5d8557b73dd33e36fef0494d903d6d250206371dccc279c",
    "ij/ImagePlus.java": "477e3f5f913b9c42feb68ea35d21836d45fd56920cccbfc5719a06822cbcfab9",
    "ij/gui/Roi.java": "200ad9f501a5860fe1486a3d2a8ed6cf6f82f7c7562a81e11d91ee7b7a04497e",
    "ij/gui/GenericDialog.java": "6905cc76a35f47c554d2d7ac5e6fd0b7bea6f3376d9a8af18f28a123a7051b20",
    "ij/gui/NonBlockingGenericDialog.java": "ccb7b8dfb91521d1067d0412a14f0b5845ee8e2e03cbc28916705abc529f6a2a",
    "ij/gui/GUI.java": "d30af7deca53018b0aaec7398f99d24dd47022e067d1888c0c6e57969226f512",
    "ij/Prefs.java": "5a35547eb3700613600fec474b78aa4d828e128c901dc5d401bdd6bbf06ece1c",
    "ij/Macro.java": "fab8720b69b9cd02700a90e192c9d46d411d8badaa68945623042cd9944152aa",
    "ij/process/ColorProcessor.java": "23da704d928476adf42a2d562360eaa7c8b740ad02fba2728074be0bac490650",
}
METHODS = ("minimum", "maximum", "median", "mean")
F32_MAX = float.fromhex("0x1.fffffep+127")


def _f32(value):
    try:
        return struct.unpack("<f", struct.pack("<f", value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def _fixture(name, dtype="uint16", size=(9, 7), *, samples=None, dims=(1, 1, 1), pos=(0, 0, 0), roi=None, recipe=None):
    width, height = size
    planes = []
    for t in range(dims[0]):
        for z in range(dims[2]):
            for c in range(dims[1]):
                values = samples if samples is not None else [recipe(t, c, z, y, x) if recipe else 1000+31*y+7*x for y in range(height) for x in range(width)]
                pack = {"uint8": "B", "uint16": "H", "rgb24": "I", "float32": "I"}[dtype]
                planes.append({"t": t, "c": c, "z": z, "bits_hex": b"".join(struct.pack("<f" if dtype == "float32" and isinstance(v, float) else "<"+pack, v) for v in values).hex()})
    return {"name": name, "dtype": dtype, "shape_tczyx": [*dims, height, width], "position_tcz": list(pos), "roi": roi, "planes": planes}


def _run(name, spec, method, radius, *, runner=False, all_stack=False):
    return {"name": name, "fixture": spec, "method": method, "radius": radius,
            "dispatch": "runner" if runner else "api", "all_stack": all_stack}


def _inputs():
    cases = {"K01": {"id": "K01", "kind": "kernels", "radii": [0, .5, 1, 1.5, 1.75, 2, 2.5, 2.85, 3, 10], "runs": []}}
    def add(number, runs=(), kind="runs", **extra):
        key = f"K{number:02d}"
        cases[key] = {"id": key, "kind": kind, "runs": list(runs), **extra}
    for number, method in zip(range(2, 6), METHODS):
        fixed = []
        for dtype in ("uint8", "uint16", "float32"):
            values = [11+7*y+3*x for y in range(7) for x in range(9)]
            values[0] = 0; values[31] = {"uint8": 200, "uint16": 60000, "float32": -17.5}[dtype]
            if dtype == "float32":
                values = list(map(float, values))
            fixed.append(_run(dtype, _fixture(dtype, dtype, samples=values), method, 1))
        add(number, fixed)
    values = [1000+31*y+7*x for y in range(7) for x in range(9)]; values[31] = 60000
    add(6, (_run("radius."+str(r), _fixture("fractional", samples=values), "median", r) for r in [0, .5, 1.5, 1.75, 2, 2.5, 2.85, 3]))
    rounding = [60001 if x < 12 or x == 12 and y < 12 else 60000 for y in range(25) for x in range(25)]
    add(7, [_run("integer_rounding", _fixture("rounding", size=(25, 25), samples=rounding), "mean", 10)])
    cancellation = (0x60ad78ec, 0xe0ad78ec, 0x3f800000, 0xbf800000)
    add(8, [_run("incremental_cancellation", _fixture("cancellation", "float32", (25, 25), samples=[cancellation[i % 4] for i in range(625)]), "mean", 3)])
    thin = []
    for dtype in ("uint8", "uint16", "float32"):
        for width, height in ((1, 1), (1, 7), (7, 1)):
            data = [float(i) if dtype == "float32" else i for i in range(1, width*height+1)]
            for method in METHODS:
                for r in (0, 1, 2):
                    thin.append(_run(f"{dtype}.{width}x{height}.{method}.{r}", _fixture("thin", dtype, (width, height), samples=data), method, r))
    add(9, thin)
    rect = {"kind": "rectangle", "xywh": [1, 1, 7, 5]}
    hole = {"kind": "holed_area", "outer_xywh": [1, 1, 7, 5], "hole_xywh": [3, 2, 3, 3]}
    add(10, [_run(name+"."+method, _fixture(name, roi=selection), method, 1, runner=True) for name, selection in (("rectangle", rect), ("hole", hole)) for method in METHODS])
    selections = (("absent", None), ("point", {"kind": "point", "xy": [2, 2]}), ("line", {"kind": "line", "xyxy": [1, 1, 7, 5]}))
    add(11, [_run(name+"."+method, _fixture(name, roi=selection), method, 1, runner=True) for name, selection in selections for method in METHODS])
    rgb = [((11+7*y+3*x)*65536 + (200-5*y-2*x)*256 + 13*x+9*y) for y in range(7) for x in range(9)]
    add(12, [_run(method, _fixture("rgb", "rgb24", samples=rgb), method, 1) for method in METHODS])
    nan_fixtures = {"mixed_qnan_zero": (0x7fc12345, 0xffc23456, 0x80000000, 0, 0x3f800000, 0xbf800000, 0x40000000),
                    "all_qnan": (0x7fc12345,), "reference_only_infinity": (0x7fc12345, 0xff800000, 0x7f800000, 0x80000000, 0, 0x3f800000, 0xbf800000)}
    add(13, [_run(name+"."+method, _fixture(name, "float32", samples=[sequence[i % len(sequence)] for i in range(63)]), method, 1) for name, sequence in nan_fixtures.items() for method in METHODS])
    add(14, [_run(scope+"."+method, _fixture("hyperstack", dims=(2, 3, 4), pos=(1, 2, 3), recipe=lambda t, c, z, y, x: 1000+100*t+30*c+5*z+2*y+x), method, 1, runner=True, all_stack=scope == "all_stack") for scope in ("current", "all_stack") for method in METHODS])
    add(15, kind="dialogs", methods=list(METHODS), fixture=_fixture("dialog"), timeline=[
        {"label": "fresh.accept2", "radius": 2, "accept": True}, {"label": "reopen.cancel3", "radius": 3, "accept": False},
        {"label": "reopen.after_cancel", "radius": None, "accept": False}])
    add(16, kind="preview", fixture=_fixture("preview", roi=hole), method="median", timeline=[
        {"label": "preview.enable", "operation": "preview", "value": True}, {"label": "preview.radius3", "operation": "radius", "value": 3},
        {"label": "preview.disable", "operation": "preview", "value": False}, {"label": "preview.cancel", "operation": "cancel"},
        {"label": "fresh.accept2", "operation": "fresh_accept", "radius": 2}])
    return cases


INPUTS = _inputs()


def _kernel(radius):
    adjusted = 1.75 if 1.5 <= radius < 1.75 else 2.85 if 2.5 <= radius < 2.85 else radius
    squared = math.floor(adjusted*adjusted)+1
    extent = math.floor(math.sqrt(squared+1e-10))
    spans = [(dy, math.floor(math.sqrt(squared-dy*dy+1e-10))) for dy in range(-extent, extent+1)]
    points = [(dx, dy) for dy, half in spans for dx in range(-half, half+1)]
    wire = [v for _, half in spans for v in (-half, half)]+[len(points), extent]
    return spans, points, wire


def _assert_kernel(actual, radius):
    spans, points, wire = _kernel(radius)
    assert actual == {"radius": radius, "api": "RankFilters.makeLineRadii(double)", "line_radii": wire,
                      "point_count": len(points), "radius_xy": [len(spans)//2]*2, "offsets_xy": [list(p) for p in points]}


def _decode(dtype, raw):
    fmt = {"uint8": "B", "uint16": "H", "float32": "f", "rgb24": "I"}[dtype]
    return list(struct.unpack("<"+fmt*(len(bytes.fromhex(raw))//struct.calcsize(fmt)), bytes.fromhex(raw)))


def _bounds(spec):
    selection = spec["roi"]
    height, width = spec["shape_tczyx"][-2:]
    return selection.get("xywh", selection.get("outer_xywh")) if selection and selection["kind"] in ("rectangle", "holed_area") else [0, 0, width, height]


def _member(spec, x, y):
    left, top, width, height = _bounds(spec)
    inside = left <= x < left+width and top <= y < top+height
    roi = spec["roi"]
    if inside and roi and roi["kind"] == "holed_area":
        a, b, w, h = roi["hole_xywh"]
        inside = not (a <= x < a+w and b <= y < b+h)
    return inside


def _nth(values, index):
    # Equal numeric zeros have different bits. Hoare selection's visit/swap
    # order is part of the observed source tie behavior; sorting loses it.
    values = list(values); low, high = 0, len(values)-1; pivot = values[index]
    while low < high:
        i, j = low, high
        while True:
            while values[i] < pivot: i += 1
            while pivot < values[j]: j -= 1
            values[i], values[j] = values[j], values[i]
            i += 1; j -= 1
            if j < index or i > index: break
        if j < index: low = i
        if index < i: high = j
        pivot = values[index]
    return pivot


def _median(values, guess, floating):
    above = [v for v in values if v > guess]; below = [v for v in values if v < guess]
    count = sum(not math.isnan(v) for v in values) if floating else len(values)
    if count == 0: return math.nan
    if len(above) > count//2: return _nth(above, len(above)-count//2-1)
    if len(below) > count//2: return _nth(below, count//2)
    return guess


def _filter_plane(spec, raw, method, radius, *, restore=True, incremental=True):
    dtype = spec["dtype"]; height, width = spec["shape_tczyx"][-2:]
    source = _decode(dtype, raw); output = list(source)
    spans, points, _ = _kernel(radius); left, top, rw, rh = _bounds(spec)
    def channel(values, shift=None):
        result = list(values)
        def at(x, y):
            return float(values[min(height-1, max(0, y))*width+min(width-1, max(0, x))])
        for y in range(top, top+rh):
            total = 0.; full = True; guess = at(left, y)
            if math.isnan(guess): guess = 0.
            for x in range(left, left+rw):
                neighborhood = [at(x+dx, y+dy) for dx, dy in points]
                if method == "mean":
                    if full or not incremental:
                        total = 0.
                        for value in neighborhood: total += value
                        full = len(spans)//2 < 2
                    else:
                        delta = 0.
                        for dy, half in spans:
                            delta -= at(x-half-1, y+dy)
                            delta += at(x+half, y+dy)
                        total += delta
                        if math.isnan(total): full = True
                    value = _f32(total/len(points))
                elif method in ("minimum", "maximum"):
                    sign = -1. if method == "minimum" else 1.
                    extreme = -F32_MAX
                    for sample in neighborhood:
                        candidate = sample*sign
                        if extreme < candidate: extreme = candidate
                    value = extreme*sign
                else:
                    if dtype == "float32": guess = at(x, y)
                    guess = _median(neighborhood, guess, dtype == "float32"); value = guess
                if dtype != "float32":
                    rounded = _f32(value+.5)
                    value = 0 if math.isnan(rounded) else 2147483647 if rounded >= 2147483647 else -2147483648 if rounded <= -2147483648 else int(rounded)
                    value &= 255 if dtype in ("uint8", "rgb24") else 65535
                result[y*width+x] = value
        return result
    if dtype == "rgb24":
        output = [0]*len(source)
        for shift in (16, 8, 0):
            filtered = channel([(v >> shift) & 255 for v in source])
            for i, v in enumerate(filtered): output[i] |= v << shift
    else:
        output = channel(source)
    if restore:
        for y in range(height):
            for x in range(width):
                if not _member(spec, x, y): output[y*width+x] = source[y*width+x]
    return output


def _assert_bits(actual, expected, dtype):
    raw = bytes.fromhex(actual)
    fmt = {"uint8": "B", "uint16": "H", "float32": "f", "rgb24": "I"}[dtype]
    assert len(raw) == len(expected)*struct.calcsize(fmt) and raw.hex() == actual
    for i, value in enumerate(expected):
        observed = raw[i*struct.calcsize(fmt):(i+1)*struct.calcsize(fmt)]
        if dtype == "float32" and math.isnan(value):
            # Arithmetic NaN payloads are not fixed by the Java language.
            # Input payload/sign bits remain exact and are checked separately.
            assert math.isnan(struct.unpack("<f", observed)[0])
        else:
            assert observed == struct.pack("<"+fmt, value), (i, value, observed.hex())


def _roi(actual, selection):
    if selection is None:
        assert actual is None; return
    assert type(actual["identity"]) is int and actual["identity"] != 0
    kind = selection["kind"]
    assert actual["type"] == {"rectangle": 0, "holed_area": 9, "point": 10, "line": 5}[kind]
    assert actual["area"] is (kind in ("rectangle", "holed_area"))
    if kind == "rectangle":
        assert actual["bounds_xywh"] == selection["xywh"] and actual["mask_bits_hex"] is None
    elif kind == "holed_area":
        assert actual["bounds_xywh"] == selection["outer_xywh"]
        left, top, w, h = selection["outer_xywh"]; a, b, iw, ih = selection["hole_xywh"]
        assert actual["mask_bits_hex"] == bytes(0 if a <= x < a+iw and b <= y < b+ih else 255 for y in range(top, top+h) for x in range(left, left+w)).hex()
    elif kind == "point":
        assert actual["bounds_xywh"] == [2, 2, 1, 1] and actual["mask_bits_hex"] == "ff"
    else:
        assert actual["bounds_xywh"] == [1, 1, 7, 5] and actual["mask_bits_hex"] is None


def _image(actual, spec, *, method=None, radius=None, all_stack=False, locked=False):
    assert actual["shape_tczyx"] == spec["shape_tczyx"] and actual["position_tcz"] == spec["position_tcz"]
    t, c, z = spec["position_tcz"]; _, channels, slices, _, _ = spec["shape_tczyx"]
    active = (t*slices+z)*channels+c+1
    assert actual["current_stack_index_1"] == active and actual["locked"] is locked
    assert actual["image_window_present"] is False
    _roi(actual["roi"], spec["roi"])
    assert len(actual["planes"]) == len(spec["planes"])
    for ordinal, (observed, original) in enumerate(zip(actual["planes"], spec["planes"]), 1):
        assert {key: observed[key] for key in ("t", "c", "z")} == {key: original[key] for key in ("t", "c", "z")}
        assert observed["stack_index_1"] == ordinal
        if method is not None and (all_stack or ordinal == active):
            _assert_bits(observed["bits_hex"], _filter_plane(spec, original["bits_hex"], method, radius), spec["dtype"])
        else:
            assert observed["bits_hex"] == original["bits_hex"]


def _calls(calls, spec, method, radius, indexes, *, preview=False):
    assert len(calls) == len(indexes)
    for call, index in zip(calls, indexes):
        plane = spec["planes"][index-1]
        assert call["stack_index_1"] == index and call["before_bits_hex"] == plane["bits_hex"]
        assert call["processor_roi_xywh"] == _bounds(spec)
        assert call["run_declaring_class"] == "ij.plugin.filter.RankFilters" and call["super_returned"] is True
        assert call["radius"] == radius and call["previewing"] is preview
        assert call["event_dispatch_thread"] is False and call["thread_identity"] != 0
        assert any(frame.startswith("ij.plugin.filter.PlugInFilterRunner.processOneImage(") for frame in call["callers"])
        _assert_bits(call["after_bits_hex"], _filter_plane(spec, plane["bits_hex"], method, radius, restore=False), spec["dtype"])
        if spec["roi"] and spec["roi"]["kind"] == "holed_area":
            actual_roi = {"identity": 1, "type": 9, "area": True, "bounds_xywh": _bounds(spec), "mask_bits_hex": call["mask_bits_hex"]}
            _roi(actual_roi, spec["roi"])
        else:
            assert call["mask_bits_hex"] is None


def _run_assert(actual, expected):
    assert actual["name"] == expected["name"] and actual["input"] == expected and actual["dispatch"] == expected["dispatch"]
    spec, method, radius = expected["fixture"], expected["method"], expected["radius"]
    _image(actual["before"], spec); _image(actual["after"], spec, method=method, radius=radius, all_stack=expected["all_stack"])
    assert actual["before"]["identity"] == actual["after"]["identity"] and actual["before"]["image_id"] == actual["after"]["image_id"]
    assert actual["before"]["roi"] == actual["after"]["roi"] and actual["selection_identity_preserved"] is True
    _assert_kernel(actual["kernel"], radius)
    if expected["dispatch"] == "api":
        assert actual["calls"] == [] and actual["returned_flags"] == -1 and actual["macro_options"] is None and actual["dialog_listener_calls"] == []
    else:
        assert actual["macro_options"] == "radius="+str(radius)+(" stack" if expected["all_stack"] else " slice")
        assert type(actual["returned_flags"]) is int and not actual["returned_flags"] & 4096
        assert bool(actual["returned_flags"] & 32) is expected["all_stack"]  # DOES_STACKS
        active = actual["before"]["current_stack_index_1"]
        _calls(actual["calls"], spec, method, radius, list(range(1, len(spec["planes"])+1)) if expected["all_stack"] else [active])
        assert actual["dialog_listener_calls"] and all(call["super_returned"] and call["valid"] and call["radius"] == radius for call in actual["dialog_listener_calls"])


def _dialog(actual, spec, radius=2, *, method=None, preview=False, filtered_radius=None):
    assert actual["class"] == "ij.gui.GenericDialog" and actual["visible"] is True and actual["displayable"] is True
    assert float(actual["radius_text"]) == radius and actual["radius_field_identity"] != 0
    assert actual["text_listeners"] == ["ij.gui.GenericDialog"] and actual["preview_item_listeners"] == ["ij.gui.GenericDialog"]
    assert actual["preview_on"] is preview and actual["preview_identity"] != 0
    assert {item["identity"] for item in actual["dialog_listeners"]} == {actual["filter_identity"], actual["runner_identity"]}
    assert [item["class"] for item in actual["dialog_listeners"]] == ["ij.plugin.filter.RankFilterReference$ObservedRank", "ij.plugin.filter.PlugInFilterRunner"]
    _image(actual["image"], spec, method=method if filtered_radius is not None else None, radius=filtered_radius, locked=True)
    if preview:
        assert actual["preview_thread"]["alive"] is True and actual["preview_thread"]["state"] == "WAITING"
    else:
        assert actual["preview_thread"] is None


def _event(actual, kind, listeners):
    assert actual["kind"] == kind and actual["event_dispatch_thread"] is True
    assert actual["event_id"] == {"radius": 900, "preview": 701, "accept": 1001, "cancel": 1001}[kind]
    assert actual["event_identity"] != 0 and actual["event_source_identity"] != 0
    assert actual["listeners"] == ["ij.gui.GenericDialog"]
    if kind in ("radius", "preview"):
        assert actual["event_source_identity"] == actual["before"]["radius_field_identity" if kind == "radius" else "preview_identity"]
        calls = [call for call in listeners if call["event_identity"] == actual["event_identity"]]
        assert len(calls) == 1 and calls[0]["event_id"] == actual["event_id"]
        assert calls[0]["source_identity"] == actual["event_source_identity"] and calls[0]["event_dispatch_thread"] is True and calls[0]["super_returned"] is True and calls[0]["valid"] is True
        if kind == "radius":
            assert calls[0]["radius"] == actual["requested"] == float(actual["after_listener"]["radius_text"])
        else:
            assert actual["after_listener"]["preview_on"] is actual["requested"]
    else:
        assert actual["button_index"] == (0 if kind == "accept" else 1)
        assert actual["button_label"] == ("  OK  " if kind == "accept" else "Cancel")


def _finish(actual, spec, method, radius=None, *, accept=False, preview_calls=False):
    assert actual["worker_joined"] is True and actual["complete"] is True
    assert actual["dialog_visible_after"] is False and actual["preview_thread_after"] is None
    assert actual["returned_flags"] & 4096 if not accept else not actual["returned_flags"] & 4096  # DONE
    _event(actual["event"], "accept" if accept else "cancel", actual["dialog_listener_calls"])
    _image(actual["after"], spec, method=method if accept else None, radius=radius)
    assert actual["event"]["before"]["image"]["identity"] == actual["after"]["identity"]
    assert actual["event"]["before"]["image"]["roi"] == actual["after"]["roi"]
    if accept:
        _calls(actual["calls"], spec, method, radius, [1])
    elif not preview_calls:
        assert actual["calls"] == []


def _environment(actual):
    assert actual == {"imagej_version": "1.54r", "java_version": "21.0.7", "java_vendor": "Azul Systems, Inc.",
                      "os_name": actual["os_name"], "awt_headless": False, "threads": 1, "live_reference_hosts": 0,
                      "host_mode": "original_unhosted_ImagePlus_and_original_AWT_dialog", "imagej_visible": False, "imagej_showing": False}
    assert actual["os_name"].startswith("Windows")


def assert_case(row):
    case = row["id"]; expected = INPUTS[case]
    assert set(row) == {"contract", "id", "input_contract", "observations", "environment", "elapsed_ms"}
    assert row["contract"] == CONTRACT and row["input_contract"] == expected
    assert type(row["elapsed_ms"]) in (int, float) and 0 <= row["elapsed_ms"] <= 5000
    _environment(row["environment"])
    actual = row["observations"]
    if case == "K01":
        assert len(actual) == 10
        for observed, radius in zip(actual, expected["radii"]): _assert_kernel(observed, radius)
        assert [actual[i]["point_count"] for i in (0, 2, 5, 8, 9)] == [5, 9, 21, 37, 325]
    elif case == "K15":
        assert len(actual) == 12
        for ordinal, observed in enumerate(actual):
            method = METHODS[ordinal//3]; step = expected["timeline"][ordinal % 3]
            assert observed["method"] == method and observed["input"] == step
            _dialog(observed["before"], expected["fixture"])
            assert observed["memory_before"] == (0 if ordinal % 3 == 0 else 2) and observed["memory_after"] == 2
            _finish(observed["finish"], expected["fixture"], method, 2 if step["accept"] else None, accept=step["accept"])
            if step["radius"] is None:
                assert observed["radius_event"] is None
            else:
                assert observed["radius_event"]["requested"] == step["radius"]
                _event(observed["radius_event"], "radius", observed["finish"]["dialog_listener_calls"])
    elif case == "K16":
        assert set(actual) == {"initial", "stages"} and len(actual["stages"]) == 5
        spec, method = expected["fixture"], expected["method"]
        _dialog(actual["initial"], spec)
        stages = actual["stages"]; final = stages[3]["event"]
        for observed, step in zip(stages, expected["timeline"]): assert observed["input"] == step
        for observed in stages[:3]:
            assert observed["after"]["image"]["identity"] == actual["initial"]["image"]["identity"]
            assert observed["after"]["image"]["roi"] == actual["initial"]["image"]["roi"]
        _event(stages[0]["event"], "preview", final["dialog_listener_calls"])
        _dialog(stages[0]["after"], spec, method=method, preview=True, filtered_radius=2)
        _event(stages[1]["event"], "radius", final["dialog_listener_calls"])
        _dialog(stages[1]["after"], spec, 3, method=method, preview=True, filtered_radius=3)
        assert stages[0]["after"]["preview_thread"]["identity"] == stages[1]["after"]["preview_thread"]["identity"]
        _event(stages[2]["event"], "preview", final["dialog_listener_calls"])
        _dialog(stages[2]["after"], spec, 3)
        _finish(final, spec, method, preview_calls=True)
        assert len(final["calls"]) >= 2
        assert all(call["super_returned"] and call["thread_name"].endswith(" Preview") and not call["event_dispatch_thread"] for call in final["calls"])
        assert len({call["thread_identity"] for call in final["calls"]}) == 1
        assert final["calls"][0]["radius"] == 2 and final["calls"][-1]["radius"] == 3
        assert all(call["radius"] in (2, 3) for call in final["calls"])
        for call in final["calls"]:
            _calls([call], spec, method, call["radius"], [1], preview=True)
        _image(stages[3]["after"], spec)
        fresh = stages[4]["event"]
        _dialog(fresh["fresh_before"], spec)
        _event(fresh["radius_event"], "radius", fresh["finish"]["dialog_listener_calls"])
        _finish(fresh["finish"], spec, method, 2, accept=True)
        _image(stages[4]["after"], spec, method=method, radius=2)
    else:
        assert len(actual) == RUN_COUNTS[CASE_IDS.index(case)] == len(expected["runs"])
        for observed, spec in zip(actual, expected["runs"]): _run_assert(observed, spec)
        if case == "K07":
            assert _decode("uint16", actual[0]["after"]["planes"][0]["bits_hex"])[312] == 60001
            assert math.floor(60000+162/325+.5) == 60000  # discriminates narrowing-before-rounding
        if case == "K08":
            request = expected["runs"][0]; spec = request["fixture"]; raw = spec["planes"][0]["bits_hex"]
            ordered = _filter_plane(spec, raw, "mean", 3)
            repeated = _filter_plane(spec, raw, "mean", 3, incremental=False)
            assert any(struct.pack("<f", a) != struct.pack("<f", b) for a, b in zip(ordered, repeated))


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, "Duplicate JSON field "+key
        result[key] = value
    return result


def _json(raw):
    def invalid(value): raise AssertionError("Nonfinite JSON token "+value)
    return json.loads(raw, object_pairs_hook=_pairs, parse_constant=invalid)


def _bytes(path, limit=MAX_PAYLOAD):
    assert path.is_file() and not path.is_symlink() and 0 <= path.stat().st_size <= limit
    raw = path.read_bytes(); assert len(raw) <= limit
    return raw


def _pin(value, limit=MAX_PAYLOAD):
    assert set(value) == {"path", "bytes", "sha256"} and type(value["bytes"]) is int and 0 <= value["bytes"] <= limit
    relative = PurePosixPath(value["path"])
    assert relative.as_posix() == value["path"] and not relative.is_absolute() and "\\" not in value["path"] and ":" not in value["path"]
    assert not any(part in (".", "..", "") for part in relative.parts)
    path = (ROOT/value["path"]).resolve(); assert path.is_relative_to(ROOT)
    raw = _bytes(path, limit)
    assert len(raw) == value["bytes"] and hashlib.sha256(raw).hexdigest() == value["sha256"]
    return raw


def _manifest(values):
    return {"members": len(values), "bytes": sum(v["bytes"] for v in values.values()),
            "canonical_sha256": hashlib.sha256(json.dumps(values, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()}


def _process(process, cap, *, java=False, mode=None):
    assert process["exit_code"] == 0 and 1 <= process["timeout_seconds"] <= cap
    _pin(process["stdout"]); _pin(process["stderr"])
    if java:
        assert "-Xmx256m" in process["argv"] and "-Djava.awt.headless=false" in process["argv"]
        assert process["argv"][-2:] == [JAVA_CLASS, mode]


def _probe(actual):
    assert actual["contract"] == CONTRACT and actual["kind"] == "environment_probe_no_K_cases" and 0 <= actual["elapsed_ms"] <= 5000
    _environment(actual["environment"])
    spec = _fixture("environment_probe_not_K", size=(2, 2), samples=[1, 2, 3, 4])
    _dialog(actual["before"], spec)
    _dialog(actual["preview_ready"], spec, method="mean", preview=True, filtered_radius=2)
    _finish(actual["cleanup"], spec, "mean", preview_calls=True)
    assert len(actual["cleanup"]["calls"]) == 1
    _calls(actual["cleanup"]["calls"], spec, "mean", 2, [1], preview=True)
    for key in ("preview_on", "preview_off"):
        _event(actual[key], "preview", actual["cleanup"]["dialog_listener_calls"])


def _raw(raw):
    assert isinstance(raw, bytes) and 0 < len(raw) <= MAX_PAYLOAD
    rows = [_json(line) for line in raw.splitlines() if line.strip()]
    assert [row["id"] for row in rows] == list(CASE_IDS)
    assert sum(row["elapsed_ms"] for row in rows) <= 80_000
    return dict(zip(CASE_IDS, rows))


def _load(path):
    path = Path(path).resolve(); assert path.is_relative_to(STAGE.resolve())
    receipt = _json(_bytes(path.parent/"capture.json"))
    assert receipt["contract"] == CONTRACT and receipt["state"] == "raw_observations_only_no_numerical_or_product_acceptance"
    assert receipt["ordered_case_ids"] == list(CASE_IDS) and receipt["case_count"] == 16 and receipt["policy_decisions"] == []
    assert 0 <= receipt["elapsed_seconds"] <= 120 and (ROOT/receipt["raw"]["path"]).resolve() == path
    raw = _pin(receipt["raw"]); rows = _raw(raw)
    _process(receipt["process"], 90, java=True, mode="--capture")
    assert receipt["process"]["stdout"] == receipt["raw"] and _pin(receipt["process"]["stderr"]) == b""
    prepared = _json(_pin(receipt["prepared"]))
    assert prepared["contract"] == CONTRACT and prepared["state"] == "prepared_environment_only_K01_K16_not_executed"
    assert receipt["harness"] == prepared["harness"] and prepared["harness"]["path"] == "scripts/capture_rank_filter_reference.py" and prepared["harness"]["sha256"] == PRODUCER_SHA
    _pin(prepared["harness"])
    plan = _json(_pin(prepared["input_plan"]))
    assert _json(_pin(receipt["input_plan"])) == plan and plan["bounds"] == BOUNDS and plan["ordered_case_ids"] == list(CASE_IDS)
    assert plan["cases"] == list(INPUTS.values()) == [row["input_contract"] for row in rows.values()]
    assert plan["environment_inputs"] == {"ImageJ_constructor": "ImageJ.NO_SHOW", "gui_scale": 1, "threads": 1,
        "image_host": "unhosted original ImagePlus", "dialogs": "original AWT GenericDialog; programmatic real component events on EDT",
        "preview": "original PlugInFilterRunner preview thread, acknowledged WAITING then readback", "reflection": "read-only state observations"}
    assert receipt["scope"] == plan["scope"] == "original numerical API and actual runner/dialog/preview observations only; no product/OS-input/commercial acceptance"
    verified = _json(_pin(prepared["verified_inputs"]))
    expected_reference = {"version": "1.54r", "commit": COMMIT, "source_archive_sha256": ARCHIVE_SHA, "runtime_archive_sha256": RUNTIME_SHA}
    assert verified["reference"] == plan["reference"] == expected_reference
    assert [(p["bytes"], p["sha256"]) for p in verified["archives"]] == [(6146010, ARCHIVE_SHA), (705099776, RUNTIME_SHA)]
    assert verified["rank_entry"]["path"] == "docs/parity/reviews/G042-rank-editor-entry-001.json" and verified["rank_entry"]["sha256"] == ENTRY_SHA
    _pin(verified["rank_entry"])
    assert verified["qualified_supervisor_source"]["sha256"] == LINEAGE_SHA
    _pin(verified["qualified_supervisor_source"])
    assert verified["reused_receipt"]["sha256"] == RECEIPT_SHA
    old = _json(_pin(verified["reused_receipt"]))
    assert old["reference"] == expected_reference
    for key, original in (("source_tree", "verified_source_tree"), ("runtime_tree", "verified_runtime_tree"), ("reused_classes", "compiled_classes")):
        assert verified[key] == _manifest(old[original])
    assert set(verified["rank_source_members"]) == set(SOURCE_PINS)
    for name, sha in SOURCE_PINS.items():
        assert verified["rank_source_members"][name] == old["verified_source_tree"][name]
        assert verified["rank_source_members"][name]["sha256"] == sha
    assert {p["path"]: p["sha256"] for p in verified["helper_sources"]} == {
        "scripts/capture_roi_boolean_reference.py": SUPERVISOR_SHA, "scripts/capture_binary_reference.py": BINARY_HELPER_SHA}
    for pin in verified["helper_sources"]: _pin(pin)
    execution = _json(_pin(prepared["execution_inputs"]))
    for key in ("driver", "harness", "input_plan", "verified_inputs"):
        assert execution[key] == prepared[key]
    _pin(prepared["driver"])
    assert {PurePosixPath(p["path"]).name: p["sha256"] for p in execution["tools"]} == {
        "capture_rank_filter_reference.py": PRODUCER_SHA, "capture_composite_min_reference.py": LINEAGE_SHA,
        "capture_roi_boolean_reference.py": SUPERVISOR_SHA, "capture_binary_reference.py": BINARY_HELPER_SHA}
    for pin in execution["tools"]: _pin(pin)
    classes = prepared["prepared_classes"]
    assert set(classes) == {"ij/plugin/filter/RankFilterReference.class", "ij/plugin/filter/RankFilterReference$ObservedRank.class", "ij/plugin/filter/RankFilterReference$Session.class"}
    prepared_dir = (ROOT/receipt["prepared"]["path"]).parent
    for name, pin in classes.items():
        content = _bytes(prepared_dir/"classes"/name, MAX_BUILD)
        assert len(content) == pin["bytes"] and hashlib.sha256(content).hexdigest() == pin["sha256"]
    for name, cap in (("compile", 60), ("version", 10), ("probe", 15)):
        process = prepared["processes"][name]; _process(process, cap, java=name == "probe", mode="--probe")
        command = _json(_bytes(prepared_dir/(("javac" if name == "compile" else name)+".command.json")))
        assert command["argv"] == process["argv"] and command["timeout_seconds"] == process["timeout_seconds"] and 0 < command["parent_remaining_seconds"] <= 120
    compile_argv = prepared["processes"]["compile"]["argv"]
    assert all(flag in compile_argv for flag in ("-proc:none", "-implicit:none", "-sourcepath", "-classpath", "-d"))
    assert _json(_pin(prepared["processes"]["probe"]["stdout"])) == prepared["probe"]
    _probe(prepared["probe"])
    consumed = _json(_pin(receipt["execution_inputs"]))
    assert consumed == {"prepared": receipt["prepared"], "harness": prepared["harness"], "input_plan": prepared["input_plan"],
        "verified_inputs": prepared["verified_inputs"], "driver": prepared["driver"], "ordered_case_ids": list(CASE_IDS), "bounds": BOUNDS}
    command = _json(_bytes(path.parent/"command.json"))
    assert command == {"argv": receipt["process"]["argv"], "parent_seconds": 120, "maximum_process_seconds": 90,
        "prepared": receipt["prepared"], "input_plan": receipt["input_plan"], "ordered_case_ids": list(CASE_IDS)}
    launched = _json(_bytes(path.parent/"reference.command.json"))
    assert launched["argv"] == receipt["process"]["argv"] and launched["timeout_seconds"] == receipt["process"]["timeout_seconds"] and 0 < launched["parent_remaining_seconds"] <= 120
    for stage in (path.parent, prepared_dir):
        files = [p for p in stage.rglob("*") if p.is_file()]
        assert not any(p.is_symlink() for p in files)
        assert sum(p.stat().st_size for p in files) <= MAX_BUILD
        assert sum(p.stat().st_size for p in files if p.suffix in (".json", ".stdout", ".stderr")) <= MAX_PAYLOAD
    return rows


def _default_raw():
    return Path(os.environ.get("IMAGEJXX_RANK_FILTER_REFERENCE", STAGE/"capture-001/reference.stdout"))


@pytest.fixture(scope="module")
def reference_cases():
    return _load(_default_raw())


def assert_raw(raw):
    rows = _raw(raw if isinstance(raw, bytes) else _bytes(Path(raw)))
    for row in rows.values(): assert_case(row)
    return rows


def assert_capture(path=None):
    rows = _load(_default_raw() if path is None else path)
    for row in rows.values(): assert_case(row)
    return rows


@pytest.mark.parametrize("case_id", CASE_IDS)
def test_rank_filter_reference(case_id, reference_cases):
    assert_case(reference_cases[case_id])
