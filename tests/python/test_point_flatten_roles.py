"""Flatten roles and detached RGB spatial/property contract; source remains intact."""
import json
import imagejxx as ij
import numpy as np
import pytest
from imagejxx import _desktop
from test_roi_style import selection_record


def action(editor, name, **values):editor.action(name, json.dumps(values))


def point(x, color, alpha=255):
    selection = ij.Selection("point", [(x, 8)])
    style = ij.PointStyle();style.marker = ij.PointMarker.CROSS;style.size = 0;style.labels = False
    selection = ij.with_point_style(selection, style)
    return ij.with_roi_style(selection, {"stroke_color": [*color, alpha]})


def render(pixels, selection):
    options = ij.PointPresentationOptions();options.color = ij.resolve_roi_style(selection).stroke_color
    return ij.render_point_style(pixels, selection, options)


def output(editor):
    return editor.native_document(json.loads(editor.document_infos())[-1]["id"]).snapshot()


@pytest.mark.parametrize("show_manager", [False, True])
@pytest.mark.parametrize("overlay", [False, True])
@pytest.mark.parametrize("active_kind", ["point", "area", "none"])
def test_original_role_order_and_active_suppression(show_manager, overlay, active_kind, tmp_path):
    pixels = np.zeros((2, 16, 24, 3), np.uint8)
    owner = ij.Document(ij.Dataset(pixels, axes="ZYXS"))
    over = point(8, [255, 0, 0], 128);saved = point(8, [0, 255, 0], 128)
    active = point(8, [0, 0, 255], 128) if active_kind == "point" else ij.Selection("rectangle", [(2, 2), (5, 5)])
    tx = owner.begin_edit();tx.set_rois([ij.ROI("shown", saved), ij.ROI("hidden", point(18, [255, 255, 255]), z=1)])
    if overlay:tx.set_overlays([selection_record(over)])
    if active_kind != "none":tx.select(active)
    owner.commit(tx);before = owner.snapshot();undo = owner.undo_steps
    editor = _desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner)
    editor.update_settings(json.dumps({"show_rois": show_manager}), False)
    expected = pixels[0].copy()
    if overlay:expected = render(expected, over)
    if show_manager:expected = render(expected, saved)
    include_active = active_kind != "none" and (not overlay or active_kind == "point")
    if include_active:
        if active_kind == "point":expected = render(expected, active)
        else:expected = ij.render_roi_style(expected, active, [255, 213, 75, 255])
    action(editor, "flatten");flat = output(editor)
    np.testing.assert_array_equal(flat.dataset.numpy("YXS"), expected)
    assert flat.selection is None and flat.rois == [] and flat.overlays == []
    provenance = flat.metadata["extensions"]["imagejxx.display_export"]
    assert [provenance[k] for k in ("overlay_rows", "manager_rows", "active_rows")] == [int(overlay), int(show_manager), int(include_active)]
    assert provenance["source_document_id"] == owner.id
    assert owner.snapshot().shares_data(before) and owner.undo_steps == undo


def test_flatten_spatial_properties_density_and_session_reopen(tmp_path):
    pixels = np.arange(3*16*24, dtype=np.uint16).reshape(3, 16, 24)
    cal = ij.SpatialCalibration(spacing=[2, 3, 4], origin=[10, 20, 30],
        direction=[1, 0, 0, 0, 0, -1, 0, 1, 0], unit="um", time_origin=7, frame_interval=2)
    owner = ij.Document(ij.Dataset(pixels, axes="ZYX", calibration=cal), "calibrated")
    tx = owner.begin_edit();tx.set_property("Info", "retained microscope notes")
    tx.set_property("user", {"experiment": [1, "two", None]});owner.commit(tx)
    density = ij.ImageJIntensityCalibration.function("uint16", "straight_line", [2, 3], "OD")
    ij.set_imagej_intensity_calibration(owner, density)
    editor = _desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner)
    action(editor, "view", crosshair=[0, 0, 2])
    before = owner.snapshot();action(editor, "flatten");flat = output(editor)
    assert flat.metadata["title"] == "Flat_calibrated"
    result = flat.dataset.calibration
    assert list(result.spacing) == [2, 3, 4] and list(result.origin) == [10, 12, 30]
    assert list(result.direction) == list(cal.direction) and result.unit == "um"
    assert result.time_origin == 7 and result.frame_interval == 2
    assert flat.metadata["properties"] == {"Info": "retained microscope notes", "user": {"experiment": [1, "two", None]}}
    provenance = flat.metadata["extensions"]["imagejxx.display_export"]
    assert provenance["source_position"] == [0, 0, 2] and provenance["display_values"]
    assert provenance["source_intensity_calibration"] == before.metadata["properties"]["imagejxx.intensity_calibration"]
    path = tmp_path/"flat.ijx";action(editor, "save_document", path=str(path))
    reopened = _desktop.Editor(str(tmp_path/"reopen.json"));reopened.open_file(str(path));restored = output(reopened)
    assert restored.metadata == flat.metadata
    assert list(restored.dataset.calibration.origin) == [10, 12, 30]
    np.testing.assert_array_equal(restored.dataset.numpy("YXS"), flat.dataset.numpy("YXS"))
    assert owner.snapshot().shares_data(before)


def test_large_properties_refuse_before_source_reads(tmp_path):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((8, 8), np.uint8), axes="YX").observe_reads(reads))
    tx = owner.begin_edit();tx.set_property("Info", "x"*400000);owner.commit(tx)
    editor = _desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner)
    editor.update_settings(json.dumps({"processing_memory_mb": 1}), False)
    count = reads.counts().plane_reads;before = owner.snapshot()
    with pytest.raises((ValueError, RuntimeError), match="budget"):action(editor, "flatten")
    assert reads.counts().plane_reads == count and owner.snapshot().shares_data(before)
    assert len(json.loads(editor.document_infos())) == 1


def test_pixel_export_does_not_copy_unused_document_properties(tmp_path):
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(np.zeros((8, 8), np.uint8), axes="YX").observe_reads(reads))
    tx = owner.begin_edit();tx.set_property("Info", "x"*400000);owner.commit(tx)
    editor = _desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner)
    editor.update_settings(json.dumps({"processing_memory_mb": 1}), False)
    count = reads.counts().plane_reads;path = tmp_path/"pixels.png"
    action(editor, "export_display", path=str(path))
    assert path.is_file() and reads.counts().plane_reads == count+1
