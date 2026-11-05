"""Pinned PointRoi drawing commands, strict styles and bounded native layout.

This tests command geometry/color/stroke/label baselines, not font or AA pixels.
"""
import gzip
import json
import math
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest

KEY = "imagejxx.point_style"
ROWS = [json.loads(line) for line in gzip.decompress((Path(__file__).parents[1] /
    "fixtures/point-presentation-imagej154r.jsonl.gz").read_bytes()).splitlines()]
MARKERS = [ij.PointMarker.HYBRID, ij.PointMarker.CROSS, ij.PointMarker.DOT, ij.PointMarker.CIRCLE]


def selection():
    return ij.Selection("multipoint", [(32.5, 40.5), (64, 65)], width=17,
        attributes={"opaque": {"kept": [None, True, "μ"]}})


@pytest.mark.parametrize("row", ROWS, ids=lambda r: f'{r["marker"]}-{r["size"]}-{r["labels"]}-{r["counters"]}')
def test_original_draw_commands_all_markers_sizes_labels_counters(row):
    selected = selection()
    if row["counters"]:
        selected = ij.with_point_annotations(selected, [ij.PointAnnotation(0), ij.PointAnnotation(2)])
    style = ij.PointStyle();style.marker = MARKERS[row["marker"]];style.size = row["size"];style.labels = row["labels"]
    selected = ij.with_point_style(selected, style)
    options = ij.PointPresentationOptions();options.color = [120, 40, 80, 200]
    result = ij.point_presentation(selected, options)
    assert len(result.glyphs) == 2
    actual = [dict(kind=int(p.kind), coordinates=p.coordinates, color=p.color,
        stroke_width=p.stroke_width, font_size=p.font_size, text=p.text) for p in result.primitives]
    assert actual == row["commands"]
    assert [(g.index, g.first_primitive, g.primitive_count) for g in result.glyphs] == [
        (0, 0, len(actual)//2), (1, len(actual)//2, len(actual)//2)]


def test_metadata_pure_sparse_roundtrip_and_no_processing_changes(tmp_path):
    selected = selection();style = ij.PointStyle();style.marker = ij.PointMarker.CIRCLE
    style.size = 5;style.show_all = True;style.cross_color = [1, 2, 3, 4];style.counter_colors = {99: [4, 3, 2, 1]}
    tagged = ij.with_point_style(selected, style)
    assert tagged.points == selected.points and tagged.width == selected.width
    assert tagged.attributes["opaque"] == selected.attributes["opaque"]
    assert tagged.attributes[KEY]["counter_colors"] == {"99": [4, 3, 2, 1]}
    assert ij.point_style(selected) is None and ij.without_point_style(tagged) == selected
    style.size = 0
    assert ij.point_style(tagged).size == 5  # independent stored value
    from imagejxx import _desktop
    reads = ij.DatasetReadObserver()
    doc = ij.Document(ij.Dataset(np.zeros((96, 96), np.uint8), axes="YX").observe_reads(reads));doc.select(tagged)
    editor = _desktop.Editor(str(tmp_path / "prefs.json"));editor.add_native_document(doc)
    n = reads.counts().plane_reads;before = doc.snapshot();undo = doc.undo_steps
    ij.point_presentation(doc.snapshot().selection)
    assert reads.counts().plane_reads == n and doc.snapshot().shares_data(before) and doc.undo_steps == undo
    path = tmp_path / "point-style.ijx";editor.action("save_document", json.dumps({"path": str(path)}))
    restored = _desktop.Editor(str(tmp_path / "reopened.json"));restored.open_file(str(path))
    assert restored.native_document(json.loads(restored.document_infos())[0]["id"]).snapshot().selection == tagged


@pytest.mark.parametrize("plane,expected", [("xy", [1]), ("xz", [0, 1]), ("yz", [0, 1])])
def test_position_visibility_show_all_and_unbound(plane, expected):
    selected = ij.with_point_annotations(selection(), [ij.PointAnnotation(2, ij.PlanePosition(0, 0, 1)), ij.PointAnnotation()])
    options = ij.PointPresentationOptions();options.position = ij.PlanePosition(0, 0, 0);options.plane = plane
    assert [g.index for g in ij.point_presentation(selected, options).glyphs] == expected
    options.position = ij.PlanePosition(1, 0, 0)
    assert [g.index for g in ij.point_presentation(selected, options).glyphs] == [1]
    options.show_all = True
    assert len(ij.point_presentation(selected, options).glyphs) == 2
    options.show_all = False;style = ij.PointStyle();style.show_all = True
    assert len(ij.point_presentation(ij.with_point_style(selected, style), options).glyphs) == 2
    assert ij.point_annotations(selected)[0].position.z == 1


def test_zoom_dpi_negative_rounding_and_clipping():
    selected = ij.Selection("point", [(-1.5, -2.5)])
    style = ij.PointStyle();style.marker = ij.PointMarker.CROSS;style.size = 2
    options = ij.PointPresentationOptions();options.origin = [10, 20];options.scale = [2, 3];options.marker_scale = 2
    result = ij.point_presentation(ij.with_point_style(selected, style), options)
    assert result.glyphs[0].center == [7, 13]
    assert result.primitives[0].coordinates == [-7, 13, 21, 13]
    options.clip = [20, 12, 22, 14]  # center outside, cross still intersects
    assert len(ij.point_presentation(ij.with_point_style(selected, style), options).glyphs) == 1
    options.clip = [100, 100, 101, 101]
    assert not ij.point_presentation(selected, options).glyphs


def test_counter_palette_fixed_overrides_and_duplicate_numbering():
    assert ij.point_counter_color(3) == [255, 200, 0, 255]
    assert ij.point_counter_color(8) == [255, 175, 175, 255]
    assert ij.point_counter_color(99) == ij.point_counter_color(99)
    selected = ij.Selection("multipoint", [(1, 1), (1, 1), (1, 1)])
    assert [p.text for p in ij.point_presentation(selected).primitives if p.kind == ij.PointPrimitiveKind.TEXT] == ["1", "2", "3"]
    selected = ij.with_point_annotations(selected, [ij.PointAnnotation(99)]*3)
    style = ij.PointStyle();style.counter_colors = {99: [1, 2, 3, 4]}
    labels = [p for p in ij.point_presentation(ij.with_point_style(selected, style)).primitives if p.kind == ij.PointPrimitiveKind.TEXT]
    assert [p.text for p in labels] == ["99"]*3 and all(p.color == [1, 2, 3, 4] for p in labels)


@pytest.mark.parametrize("field", ["max_points", "max_primitives", "max_label_chars", "max_working_bytes"])
def test_exact_native_limits_and_offscreen_output(field):
    selected = selection();result = ij.point_presentation(selected);limit = ij.PointPresentationLimits()
    exact = {"max_points": 2, "max_primitives": len(result.primitives), "max_label_chars": 2, "max_working_bytes": result.working_bytes}[field]
    setattr(limit, field, exact);assert len(ij.point_presentation(selected, limits=limit).glyphs) == 2
    setattr(limit, field, exact-1)
    with pytest.raises((ValueError, RuntimeError)):ij.point_presentation(selected, limits=limit)
    if field in ("max_primitives", "max_label_chars"):
        options = ij.PointPresentationOptions();options.clip = [1000, 1000, 1001, 1001]
        assert not ij.point_presentation(selected, options, limit).glyphs


@pytest.mark.parametrize("field,value", [("version", 2), ("marker", 4), ("size", 7), ("size", True),
    ("labels", 1), ("show_all", "false"), ("cross_color", [0, 0, 0, 256]),
    ("counter_colors", {"01": [0]*4}), ("counter_colors", {"100": [0]*4}), ("extra", 1)])
def test_strict_metadata_and_explicit_recovery(field, value):
    selected = ij.with_point_style(selection(), ij.PointStyle());attrs = selected.attributes
    attrs[KEY][field] = value
    malformed = ij.Selection("multipoint", selected.points, attributes=attrs)
    with pytest.raises(ValueError):ij.point_style(malformed)
    with pytest.raises(ValueError):ij.with_point_style(malformed, ij.PointStyle())
    assert ij.point_style(ij.without_point_style(malformed)) is None
    assert malformed.attributes[KEY][field] == value


@pytest.mark.parametrize("field,value", [("scale", [0, 1]), ("scale", [math.inf, 1]), ("origin", [math.nan, 0]),
    ("marker_scale", 0), ("marker_scale", math.inf), ("clip", [2, 0, 1, 2]), ("clip", [0, 0, math.inf, 1])])
def test_invalid_view(field, value):
    options = ij.PointPresentationOptions();setattr(options, field, value)
    with pytest.raises(ValueError):ij.point_presentation(selection(), options)


def test_explicit_wire_style_and_native_envelope_preserve_original():
    from test_point_roi_interchange import mapping, raw, EXTENT
    record = raw();record.point_type = 255;record.point_size = 254
    wire = ij.encode_imagej_roi(record)
    assert ij.encode_imagej_roi(ij.decode_imagej_roi(wire)) == wire
    with pytest.raises(ValueError):ij.imagej_point_style(record)
    source = ij.annotated_imagej_point_mapping(ij.map_imagej_roi(record, mapping()))
    style = ij.PointStyle();style.marker = ij.PointMarker.DOT;style.size = 6;style.labels = True;style.show_all = True
    style.counter_colors = {99: [1, 2, 3, 4]}
    old = source.annotation
    row = ij.PlanarAnnotation(old.name, ij.with_point_style(old.selection, style), old.plane,
        old.coordinate, old.t, old.c, attributes=old.attributes)
    mapped = ij.rebind_imagej_roi_mapping(source, EXTENT, row)
    projected = ij.imagej_roi_annotation_projection(mapped.annotation, EXTENT, record)
    assert projected.point_type == 2 and projected.point_size == 6
    assert projected.options == record.options|1024 and projected.properties == record.properties
    assert KEY not in projected.roi.selection.attributes
    assert ij.imagej_point_style(projected).size == 6
    reopened = ij.decode_imagej_roi_envelope(ij.encode_imagej_roi_native_envelope(mapped, projected))
    assert reopened.annotation.selection == row.selection
    assert ij.encode_imagej_roi(reopened.source_record) == wire
    assert ij.point_style(reopened.annotation.selection).counter_colors == {99: [1, 2, 3, 4]}
    assert not ij.imagej_point_style(projected).show_all  # native-only display policy


@pytest.mark.parametrize("field,value,error", [("size", True, TypeError), ("size", 7, ValueError),
    ("labels", 1, TypeError), ("show_all", 0, TypeError), ("cross_color", [1, 2, 3, -1], OverflowError),
    ("counter_colors", {True: [0]*4}, TypeError), ("counter_colors", {100: [0]*4}, ValueError)])
def test_python_setters_validate_before_mutation(field, value, error):
    style = ij.PointStyle()
    with pytest.raises(error):setattr(style, field, value)
    assert style.size == 1 and style.labels and not style.show_all


def test_annotation_decode_admission_and_large_output_refusal():
    selected = ij.with_point_annotations(selection(), [ij.PointAnnotation(0), ij.PointAnnotation(99)])
    result = ij.point_presentation(selected);limit = ij.PointPresentationLimits()
    limit.max_working_bytes = result.working_bytes-1
    with pytest.raises((ValueError, RuntimeError)):ij.point_presentation(selected, limits=limit)
    limit.max_working_bytes = result.working_bytes
    assert len(ij.point_presentation(selected, limits=limit).glyphs) == 2
    # Cardinality limits refuse before allocating the large flat output.
    many = ij.Selection("multipoint", [(1, 1)]*100000)
    limit = ij.PointPresentationLimits();limit.max_primitives = 1
    with pytest.raises((ValueError, RuntimeError)):ij.point_presentation(many, limits=limit)
    options = ij.PointPresentationOptions();options.clip = [1000, 1000, 1001, 1001]
    assert not ij.point_presentation(many, options, limit).glyphs


@pytest.mark.parametrize("points", [[(math.inf, 1)], [(math.nan, 0)], [(1e308, 1)]])
def test_nonfinite_input_or_device_overflow_refuses(points):
    selected = ij.Selection("point", points);options = ij.PointPresentationOptions();options.scale = [2, 1]
    with pytest.raises(ValueError):ij.point_presentation(selected, options)
