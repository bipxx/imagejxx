"""Pre-source-read configured admission and detached point export inputs."""
import json
from pathlib import Path

import imagejxx as ij
import numpy as np
import pytest
from imagejxx import _desktop
from test_roi_style import selection_record


def action(editor, name, **values):editor.action(name, json.dumps(values))


def fixture(tmp_path, size=16, labels=False):
    source = np.zeros((size, size, 3), np.uint8)
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(source, axes="YXS").observe_reads(reads))
    selection = ij.Selection("multipoint", [(4, 4), (9, 9)])
    style = ij.PointStyle();style.marker = ij.PointMarker.CROSS;style.size = 0;style.labels = labels
    selection = ij.with_point_style(selection, style)
    tx = owner.begin_edit();tx.set_overlays([selection_record(selection)]);owner.commit(tx)
    editor = _desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner)
    return editor, owner, reads, source, selection


@pytest.mark.parametrize("command", ["flatten", "export_display", "export_pdf"])
def test_low_configured_cap_refuses_before_provider_read(command, tmp_path):
    editor, owner, reads, source, selection = fixture(tmp_path, 128)
    editor.update_settings(json.dumps({"processing_memory_mb": 1}), False)
    before = owner.snapshot();n = reads.counts().plane_reads;count = len(json.loads(editor.document_infos()))
    params = {} if command == "flatten" else {"path": str(tmp_path/("output.pdf" if command == "export_pdf" else "output.png"))}
    with pytest.raises((ValueError, RuntimeError), match="budget"):
        action(editor, command, **params)
    assert reads.counts().plane_reads == n and owner.snapshot().shares_data(before)
    assert len(json.loads(editor.document_infos())) == count and not list(tmp_path.glob("output.*"))


@pytest.mark.parametrize("failure", ["missing_font", "point_style", "palette", "pdf_dpi", "bad_format"])
def test_bad_options_and_point_fonts_refuse_before_read(failure, tmp_path):
    editor, owner, reads, source, selection = fixture(tmp_path, labels=True)
    overlays = owner.snapshot().overlays
    if failure == "missing_font":overlays[0]["font_path"] = str(tmp_path/"missing.ttf")
    if failure == "point_style":overlays[0]["imagejxx.point_style"]["version"] = 9
    if failure == "palette":overlays[0]["imagejxx.point_style"]["counter_colors"] = {"100": [1, 2, 3, 4]}
    tx = owner.begin_edit();tx.set_overlays(overlays);owner.commit(tx)
    n = reads.counts().plane_reads;before = owner.snapshot()
    command = "export_pdf" if failure == "pdf_dpi" else "export_display" if failure == "bad_format" else "flatten"
    params = {"path": str(tmp_path/"bad.pdf"), "dpi": 0} if failure == "pdf_dpi" else {"path": str(tmp_path/"bad.unknown")} if failure == "bad_format" else {}
    with pytest.raises((ValueError, RuntimeError)):action(editor, command, **params)
    assert reads.counts().plane_reads == n and owner.snapshot().shares_data(before)
    assert len(json.loads(editor.document_infos())) == 1 and not list(tmp_path.glob("bad.*"))


def test_small_unlabelled_point_export_fits_low_budget_and_reads_once(tmp_path):
    editor, owner, reads, source, selection = fixture(tmp_path)
    editor.update_settings(json.dumps({"processing_memory_mb": 1}), False)
    before = owner.snapshot();n = reads.counts().plane_reads
    action(editor, "flatten")
    assert reads.counts().plane_reads == n+1 and owner.snapshot().shares_data(before)
    options = ij.PointPresentationOptions();options.color = [255, 204, 0, 255]
    result = editor.native_document(json.loads(editor.document_infos())[-1]["id"]).snapshot().dataset.numpy("YXS")
    np.testing.assert_array_equal(result, ij.render_point_style(source, selection, options))


def test_invisible_point_labels_do_not_load_unused_font(tmp_path):
    editor, owner, reads, source, selection = fixture(tmp_path, labels=True)
    overlays = owner.snapshot().overlays;overlays[0]["slice_index"] = 1;overlays[0]["font_path"] = str(tmp_path/"missing.ttf")
    tx = owner.begin_edit();tx.set_overlays(overlays);owner.commit(tx)
    editor.update_settings(json.dumps({"processing_memory_mb": 1, "point_show_all": True}), False)
    n = reads.counts().plane_reads;action(editor, "flatten")
    assert reads.counts().plane_reads == n+1
    result = editor.native_document(json.loads(editor.document_infos())[-1]["id"]).snapshot().dataset.numpy("YXS")
    np.testing.assert_array_equal(result, source)


def test_mixed_sizes_and_two_font_paths_keep_owned_coverage_and_draw_order(tmp_path):
    import os
    path = Path(os.environ.get("IMAGEJXX_FONT") or (os.environ.get("WINDIR", "C:/Windows")+"/Fonts/segoeui.ttf"))
    if os.name != "nt" and not os.environ.get("IMAGEJXX_FONT"):
        import sys
        path = Path("/System/Library/Fonts/Supplemental/Arial.ttf" if sys.platform == "darwin" else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
    assert path.is_file()
    source = np.full((48, 64, 3), [31, 72, 109], np.uint8);reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(source, axes="YXS").observe_reads(reads));overlays = [];expected = source.copy()
    fonts = [8, 9, 11, 12, 14, 18, 20]
    for index in range(7):
        selected = ij.Selection("multipoint", [(12+index, 10), (30, 24+index)])
        style = ij.PointStyle();style.marker = ij.PointMarker.CIRCLE;style.size = index
        selected = ij.with_point_style(selected, style)
        selected = ij.with_roi_style(selected, {"stroke_color": f"#80{20+index*20:02x}4050"})
        spelling = str(path) if index % 2 else path.as_posix()
        overlays.append({**selection_record(selected), "font_path": spelling})
        options = ij.PointPresentationOptions();options.color = [20+index*20, 64, 80, 128]
        expected = ij.render_point_style(expected, selected, options, _desktop.load_point_raster_font(spelling, fonts[index]))
    tx = owner.begin_edit();tx.set_overlays(overlays);owner.commit(tx)
    editor = _desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner)
    editor.update_settings(json.dumps({"processing_memory_mb": 16}), False)
    n = reads.counts().plane_reads;action(editor, "flatten");assert reads.counts().plane_reads == n+1
    result = editor.native_document(json.loads(editor.document_infos())[-1]["id"]).snapshot().dataset.numpy("YXS")
    np.testing.assert_array_equal(result, expected)


def test_processing_budget_zero_retains_unlimited_setting_semantics(tmp_path):
    editor, owner, reads, source, selection = fixture(tmp_path, 128)
    editor.update_settings(json.dumps({"processing_memory_mb": 1}), False)
    n = reads.counts().plane_reads
    with pytest.raises((ValueError, RuntimeError)):action(editor, "flatten")
    assert reads.counts().plane_reads == n
    editor.update_settings(json.dumps({"processing_memory_mb": 0}), False)
    action(editor, "flatten");assert reads.counts().plane_reads == n+1
