"""Explicit generic ROI display context, independent original-command pixels."""
import imagejxx as ij
import numpy as np
import pytest
from test_point_raster import ROWS, selected, font, oracle


@pytest.mark.parametrize("row", ROWS, ids=lambda r: f'{r["marker"]}-{r["size"]}-{r["labels"]}-{r["counters"]}')
def test_original_command_pixels_through_generic_roi_context(row):
    source = np.full((96, 96, 3), [31, 72, 109], np.uint8)
    selection = selected(row);before = selection.attributes
    options = ij.PointPresentationOptions();options.color = [1, 2, 3, 255]
    labels = [p for p in row["commands"] if p["kind"] == 4]
    supplied = font(labels[0]["font_size"]) if labels else None
    actual = ij.render_roi_style(source, selection, [120, 40, 80, 200], point_options=options, font=supplied)
    np.testing.assert_array_equal(actual, oracle(source, row["commands"], supplied))
    assert actual.dtype == np.uint8 and not np.shares_memory(actual, source)
    assert selection.attributes == before and options.color == [1, 2, 3, 255]
    np.testing.assert_array_equal(source, np.full_like(source, [31, 72, 109]))


@pytest.mark.parametrize("stroke", ["#80112233", None, [2, 3, 4, 0]])
def test_resolved_roi_color_and_legacy_opt_in(stroke):
    selection = selected(next(r for r in ROWS if not r["labels"] and not r["counters"]))
    selection = ij.with_roi_style(selection, {"stroke_color": stroke})
    source = np.zeros((96, 96, 3), np.uint8);options = ij.PointPresentationOptions()
    expected_options = ij.PointPresentationOptions()
    expected_options.color = ij.resolve_roi_style(selection, [7, 8, 9, 100]).stroke_color
    np.testing.assert_array_equal(ij.render_roi_style(source, selection, [7, 8, 9, 100], point_options=options),
        ij.render_point_style(source, selection, expected_options))
    # Existing positional calls retain the mask meaning even with tagged metadata.
    np.testing.assert_array_equal(ij.render_roi_style(source, selection),
        ij.render_roi_style(source, ij.without_point_style(selection)))


@pytest.mark.parametrize("failure", ["bytes", "work", "font", "font-size", "clip", "scale", "version", "palette"])
def test_point_context_admission_errors_preserve_source(failure):
    row = next(r for r in ROWS if r["labels"] and not r["counters"])
    selection = selected(row);options = ij.PointPresentationOptions();limits = ij.PointRasterLimits()
    supplied = font(next(p["font_size"] for p in row["commands"] if p["kind"] == 4))
    if failure == "bytes":limits.max_working_bytes = 1
    if failure == "work":limits.max_pixel_samples = 1
    if failure == "font":supplied = None
    if failure == "font-size":supplied = font(123)
    if failure == "clip":options.clip = [4, 0, 2, 3]
    if failure == "scale":options.marker_scale = float("inf")
    if failure == "version":selection = selection.with_attributes({"imagejxx.point_style": {"version": 2}})
    if failure == "palette":
        attributes = selection.attributes;attributes["imagejxx.point_style"]["counter_colors"] = {"100": [1, 2, 3, 4]}
        selection = selection.with_attributes(attributes)
    # Invalid metadata/resources refuse with the borrowed source unchanged.
    source = np.zeros((16, 16, 3), np.uint8);before = source.copy()
    with pytest.raises((ValueError, RuntimeError, OverflowError)):
        ij.render_roi_style(source, selection, point_options=options, font=supplied, point_limits=limits)
    np.testing.assert_array_equal(source, before)


@pytest.mark.parametrize("tagged", [False, True])
def test_cancelled_generic_context(tagged):
    selection = selected(next(r for r in ROWS if not r["labels"] and not r["counters"])) if tagged else ij.Selection("rectangle", [(2, 2), (6, 6)])
    token = ij.CancellationToken();token.cancel()
    with pytest.raises(ij.OperationCancelled):
        ij.render_roi_style(np.zeros((12, 12, 3), np.uint8), selection, point_options=ij.PointPresentationOptions(), cancellation=token)


@pytest.mark.parametrize("kind", ["point", "rectangle", "line"])
def test_untagged_legacy_route_ignores_point_only_context(kind):
    selection = ij.Selection(kind, [(2, 2)] if kind == "point" else [(2, 2), (6, 6)])
    source = np.arange(12*12*3, dtype=np.uint8).reshape(12, 12, 3)[::-1, ::-1]
    options = ij.PointPresentationOptions();options.marker_scale = float("inf")
    limits = ij.PointRasterLimits();limits.max_working_bytes = 0
    np.testing.assert_array_equal(ij.render_roi_style(source, selection, [7, 8, 9, 100], point_options=options, font=font(123), point_limits=limits),
        ij.render_roi_style(source, selection, [7, 8, 9, 100]))
