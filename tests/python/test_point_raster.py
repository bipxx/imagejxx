"""Analytic pixel contract driven by pinned original draw commands, not Java AA.

The independent oracle evaluates all pixels, unlike the native bounded scan.
Font masks below are synthetic coverage inputs, not distributed font assets.
"""
import gzip
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

ROWS = [json.loads(x) for x in gzip.decompress((Path(__file__).parents[1] /
    "fixtures/point-presentation-imagej154r.jsonl.gz").read_bytes()).splitlines()]
MARKERS = [ij.PointMarker.HYBRID, ij.PointMarker.CROSS, ij.PointMarker.DOT, ij.PointMarker.CIRCLE]


def font(size):
    masks = [bytes([255, 0, 32+d*20, 64+d*15, 128, 255]) for d in range(10)]
    return ij.PointRasterFont(size, [ij.PointRasterGlyph(2, 3, -1, -3, 3.5, m) for m in masks])


def oracle(source, commands, supplied_font=None, clip=None):
    result = source.astype(float).copy()
    height, width = result.shape[:2]
    # Pixel cells are integrated at sixteen centers for geometry. No native
    # bounds code, presentation API or native intermediate enters the oracle.
    yy, xx = np.mgrid[:height, :width]
    sx = xx[..., None, None] + (np.arange(4)[None, None, None, :] + .5)/4
    sy = yy[..., None, None] + (np.arange(4)[None, None, :, None] + .5)/4
    for command in commands:
        kind = command["kind"];q = command["coordinates"];ink = command["color"]
        draws = []
        if kind == 4:
            cursor = q[0]
            for code in command["text"]:
                g = supplied_font.digits[int(code)]
                coverage = np.zeros((height, width))
                x = int(np.floor(cursor))+g.offset_x;y = int(np.floor(q[1]))+g.offset_y
                values = np.frombuffer(g.coverage, np.uint8).reshape(g.height, g.width)/255
                for gy in range(g.height):
                    for gx in range(g.width):
                        px, py = x+gx, y+gy
                        if 0 <= px < width and 0 <= py < height:
                            fraction = 1
                            if clip is not None:
                                fraction = max(0, min(px+1, clip[2])-max(px, clip[0]))*max(0, min(py+1, clip[3])-max(py, clip[1]))
                            coverage[py, px] = values[gy, gx]*fraction
                draws.append(coverage);cursor += g.advance
        else:
            if kind in (0, 1):
                pad = command["stroke_width"]/2 if kind == 0 else 0
                inside = (sx >= min(q[0], q[2])-pad) & (sx < max(q[0], q[2])+pad) & (sy >= min(q[1], q[3])-pad) & (sy < max(q[1], q[3])+pad)
            else:
                # All point ellipses are circles. Independent squared-distance
                # classification rather than normalized ellipse coordinates.
                radius = (q[2]-q[0])/2;cx = (q[0]+q[2])/2;cy = (q[1]+q[3])/2
                distance = (sx-cx)**2+(sy-cy)**2
                if kind == 2:
                    inside = distance <= radius**2
                else:
                    half = command["stroke_width"]/2
                    inside = (distance <= (radius+half)**2) & ((radius-half <= 0) | (distance > max(0, radius-half)**2))
            if clip is not None:
                inside = inside & (sx >= clip[0]) & (sx < clip[2]) & (sy >= clip[1]) & (sy < clip[3])
            draws.append(inside.mean(axis=(-2, -1)))
        for coverage in draws:
            alpha = coverage[..., None]*ink[3]/255
            # Multiplication order matches the documented alpha definition.
            result = np.floor(result*(1-alpha)+np.array(ink[:3])*alpha+.5)
    return result.astype(np.uint8)


def selected(row):
    selection = ij.Selection("multipoint", [(32.5, 40.5), (64, 65)], width=17,
        attributes={"opaque": {"preserve": [None, "渭"]}})
    if row["counters"]:
        selection = ij.with_point_annotations(selection, [ij.PointAnnotation(0), ij.PointAnnotation(2)])
    style = ij.PointStyle();style.marker = MARKERS[row["marker"]];style.size = row["size"];style.labels = row["labels"]
    return ij.with_point_style(selection, style)


@pytest.mark.parametrize("row", ROWS, ids=lambda r: f'{r["marker"]}-{r["size"]}-{r["labels"]}-{r["counters"]}')
def test_analytic_pixels_for_original_commands(row):
    source = np.full((96, 96, 3), [31, 72, 109], np.uint8)
    selection = selected(row);before = selection.attributes
    options = ij.PointPresentationOptions();options.color = [120, 40, 80, 200]
    labels = [p for p in row["commands"] if p["kind"] == 4]
    supplied = font(labels[0]["font_size"]) if labels else None
    actual = ij.render_point_style(source, selection, options, supplied)
    np.testing.assert_array_equal(actual, oracle(source, row["commands"], supplied))
    np.testing.assert_array_equal(source, np.full_like(source, [31, 72, 109]))
    assert actual.dtype == np.uint8 and not np.shares_memory(actual, source)
    assert selection.attributes == before and selection.width == 17


@pytest.mark.parametrize("marker", range(4))
@pytest.mark.parametrize("scale", [.5, 1, 2, 3])
def test_fractional_clip_relative_coordinates_and_marker_scale(marker, scale):
    row = next(r for r in ROWS if r["marker"] == marker and r["size"] == 4 and not r["labels"] and not r["counters"])
    options = ij.PointPresentationOptions();options.color = [120, 40, 80, 200]
    options.marker_scale = scale;options.relative_primitives = True;options.clip = [30.3, 35.7, 65.2, 72.8]
    commands = []
    half = len(row["commands"])//2
    for i, command in enumerate(row["commands"]):
        p = dict(command);center = [33, 41] if i < half else [64, 65]
        p["coordinates"] = [center[j % 2]+(v-center[j % 2])*scale for j, v in enumerate(p["coordinates"])]
        p["stroke_width"] *= scale;commands.append(p)
    source = np.zeros((96, 96, 3), np.uint8)
    np.testing.assert_array_equal(ij.render_point_style(source, selected(row), options), oracle(source, commands, clip=options.clip))


def test_duplicate_draw_order_positions_and_explicit_visibility():
    selection = ij.Selection("multipoint", [(8, 8), (8, 8)])
    style = ij.PointStyle();style.marker = ij.PointMarker.CROSS;style.size = 0;style.labels = False
    style.counter_colors = {0: [200, 0, 0, 128], 1: [0, 0, 200, 128]}
    selection = ij.with_point_style(ij.with_point_annotations(selection, [ij.PointAnnotation(0, ij.PlanePosition(0, 0, 0)), ij.PointAnnotation(1, ij.PlanePosition(0, 0, 1))]), style)
    source = np.zeros((16, 16, 3), np.uint8)
    options = ij.PointPresentationOptions();options.position = ij.PlanePosition(0, 0, 0)
    first = ij.render_point_style(source, selection, options)
    assert first[8, 8, 0] > 0 and first[8, 8, 2] == 0
    options.show_all = True;both = ij.render_point_style(source, selection, options)
    assert both[8, 8, 0] < first[8, 8, 0] and both[8, 8, 2] > 0
    options.show_all = False;options.plane = "xz"
    np.testing.assert_array_equal(both, ij.render_point_style(source, selection, options))
    options.position = ij.PlanePosition(2, 0, 0)
    np.testing.assert_array_equal(source, ij.render_point_style(source, selection, options))


@pytest.mark.parametrize("failure", ["bytes", "samples", "vertices", "primitives", "labels", "font", "font-size", "clip", "scale", "unknown-style"])
def test_admission_failures_preserve_input(failure):
    row = next(r for r in ROWS if r["labels"] and not r["counters"])
    selection = selected(row);source = np.zeros((96, 96, 3), np.uint8)
    options = ij.PointPresentationOptions();limits = ij.PointRasterLimits();supplied = font(next(p["font_size"] for p in row["commands"] if p["kind"] == 4))
    # All explicit failures occur before NumPy data conversion/output publication.
    if failure == "bytes":limits.max_working_bytes = 1
    if failure == "samples":limits.max_pixel_samples = 1
    if failure == "vertices":limits.presentation.max_points = 1
    if failure == "primitives":limits.presentation.max_primitives = 0
    if failure == "labels":limits.presentation.max_label_chars = 0
    if failure == "font":supplied = None
    if failure == "font-size":supplied = font(123)
    if failure == "clip":options.clip = [4, 0, 2, 3]
    if failure == "scale":options.marker_scale = float("inf")
    if failure == "unknown-style":selection = selection.with_attributes({"imagejxx.point_style": {"version": 2}})
    with pytest.raises((ValueError, RuntimeError, OverflowError)):
        ij.render_point_style(source, selection, options, supplied, limits)
    assert not source.any()


@pytest.mark.parametrize("shape,dtype", [((10, 10), np.uint8), ((10, 10, 4), np.uint8), ((10, 10, 3), np.float32), ((0, 10, 3), np.uint8)])
def test_source_contract(shape, dtype):
    with pytest.raises(ValueError):ij.render_point_style(np.zeros(shape, dtype), ij.Selection("point", [(4, 4)]))


def test_font_boundaries_and_cancelled_native_call():
    for args in [(2, 3, 0, -3, 2, b"short"), (5000, 0, 0, 0, 2, b""), (0, 0, 0, 0, float("nan"), b"")]:
        with pytest.raises(ValueError):ij.PointRasterGlyph(*args)
    token = ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.render_point_style(np.zeros((8, 8, 3), np.uint8), ij.Selection("point", [(3, 3)]), cancellation=token)


def test_numpy_strides_and_empty_clip():
    source = np.arange(30*32*3, dtype=np.uint8).reshape(30, 32, 3)[::-2, ::-2, ::-1]
    selection = ij.Selection("point", [(4, 4)])
    options = ij.PointPresentationOptions();options.clip = [0, 0, 0, 0]
    np.testing.assert_array_equal(source, ij.render_point_style(source, selection, options))
    options.clip = None
    np.testing.assert_array_equal(ij.render_point_style(source, selection, options), ij.render_point_style(source.copy(), selection, options))


def test_multidigit_labels_and_offscreen_font_bearings():
    row = next(r for r in ROWS if r["marker"] == 1 and r["size"] == 0 and r["labels"] and not r["counters"])
    prototype = row["commands"][:len(row["commands"])//2]
    commands = []
    for index in range(12):
        for original in prototype:
            command = dict(original)
            command["coordinates"] = [v-25 if j % 2 == 0 else v-33 for j, v in enumerate(original["coordinates"])]
            if command["kind"] == 4:
                command["text"] = str(index+1);command["coordinates"][2:] = [0, 0]
            commands.append(command)
    selection = ij.Selection("multipoint", [(8, 8)]*12)
    style = ij.PointStyle();style.marker = ij.PointMarker.CROSS;style.size = 0
    selection = ij.with_point_style(selection, style)
    options = ij.PointPresentationOptions();options.color = [120, 40, 80, 200]
    supplied = font(8);source = np.zeros((32, 32, 3), np.uint8)
    np.testing.assert_array_equal(ij.render_point_style(source, selection, options, supplied), oracle(source, commands, supplied))
    # Explicit font bearings, not guessed host text bounds, own label clipping.
    digits = [ij.PointRasterGlyph(1, 1, 200, -10, 1, bytes([255])) for _ in range(10)]
    far_font = ij.PointRasterFont(8, digits)
    selection = ij.Selection("multipoint", [(-200, 8), (-200, 8)])
    selection = ij.with_point_style(selection, style)
    options.clip = [0, 0, 32, 32]
    actual = ij.render_point_style(source, selection, options, far_font)
    assert actual[8, 2].any()  # Anchor offscreen, bitmap bearing brings text in.


def test_huge_offscreen_points_and_resource_work_bound():
    selection = ij.Selection("multipoint", [(1e300, -1e300)]*2)
    style = ij.PointStyle();style.marker = ij.PointMarker.CIRCLE;style.labels = False
    selection = ij.with_point_style(selection, style)
    source = np.zeros((8, 8, 3), np.uint8);limits = ij.PointRasterLimits()
    limits.max_pixel_samples = 1000
    np.testing.assert_array_equal(source, ij.render_point_style(source, selection, limits=limits))
    options = ij.PointPresentationOptions();options.marker_scale = 1e308
    with pytest.raises(ValueError):ij.render_point_style(source, selection, options)
