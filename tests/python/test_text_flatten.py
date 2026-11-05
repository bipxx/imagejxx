"""Captured text resource admission and mixed display exports in both hosts."""
import json
import os
from pathlib import Path
import imagejxx as ij
import numpy as np
import pytest
from imagejxx import _desktop
from test_roi_style import selection_record


def action(editor, name, **values):editor.action(name, json.dumps(values))


def fixture(tmp_path, text="A12\nβ 2", size=16, anchor=(2.75, 2.25), **values):
    pixels = np.full((48, 64, 3), [31, 70, 109], np.uint8)
    reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes="YXS").observe_reads(reads))
    row = {"kind": "text", "points": [list(anchor)], "text": text, "font_size": size, "color": "#40d08080", **values}
    tx = owner.begin_edit();tx.set_overlays([row]);owner.commit(tx)
    editor = _desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner)
    return editor, owner, reads, pixels, row


def output(editor):return editor.native_document(json.loads(editor.document_infos())[-1]["id"]).snapshot()


@pytest.mark.parametrize("command", ["flatten", "export_display", "export_pdf"])
@pytest.mark.parametrize("failure", ["font", "size", "budget"])
def test_text_errors_refuse_before_source_reads(command, failure, tmp_path):
    values = {"font_path": str(tmp_path/"missing.ttf")} if failure == "font" else {}
    editor, owner, reads, pixels, row = fixture(tmp_path, size=0 if failure == "size" else 16, **values)
    if failure == "budget":editor.update_settings(json.dumps({"processing_memory_mb": 1}), False)
    before = owner.snapshot();n = reads.counts().plane_reads
    path = tmp_path/("out.pdf" if command == "export_pdf" else "out.png")
    with pytest.raises((ValueError, RuntimeError)):
        action(editor, command, **({} if command == "flatten" else {"path": str(path)}))
    assert owner.snapshot().shares_data(before) and reads.counts().plane_reads == n
    assert len(json.loads(editor.document_infos())) == 1 and not path.exists()


@pytest.mark.parametrize("text", ["", "\n\n"])
def test_no_glyph_rows_do_not_acquire_unused_font(text, tmp_path):
    editor, owner, reads, pixels, row = fixture(tmp_path, text=text, font_path=str(tmp_path/"missing.ttf"))
    n = reads.counts().plane_reads;action(editor, "flatten")
    np.testing.assert_array_equal(output(editor).dataset.numpy("YXS"), pixels)
    assert reads.counts().plane_reads == n+1


def test_parent_ineligible_text_does_not_acquire_font(tmp_path):
    pixels = np.zeros((2, 16, 16, 3), np.uint8);reads = ij.DatasetReadObserver()
    owner = ij.Document(ij.Dataset(pixels, axes="ZYXS").observe_reads(reads))
    tx = owner.begin_edit();tx.set_overlays([{"kind": "text", "points": [[1, 1]], "text": "A", "font_path": str(tmp_path/"missing.ttf"), "slice_index": 1}]);owner.commit(tx)
    editor = _desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner)
    editor.update_settings(json.dumps({"point_show_all": True}), False)
    n = reads.counts().plane_reads;action(editor, "flatten")
    np.testing.assert_array_equal(output(editor).dataset.numpy("YXS"), pixels[0]);assert reads.counts().plane_reads == n+1


@pytest.mark.parametrize("text,size,anchor", [("A12 A12", 1, (-3.25, 2.75)), ("βΩ é\n汉字", 9, (2.75, 2.25)), ("A\nB\n12", 32, (-9.75, -12.25)), ("A12", 512, (-3.25, 0)), ("A12", 16, (1e300, -1e300))])
def test_text_output_export_reopen_and_source_preservation(text, size, anchor, tmp_path):
    editor, owner, reads, pixels, row = fixture(tmp_path, text=text, size=size, anchor=anchor)
    before = owner.snapshot();undo = owner.undo_steps;n = reads.counts().plane_reads
    action(editor, "flatten");flat = output(editor)
    assert reads.counts().plane_reads == n+1 and owner.snapshot().shares_data(before) and owner.undo_steps == undo
    actual = flat.dataset.numpy("YXS");assert actual.dtype == np.uint8
    if anchor[0] == 1e300:np.testing.assert_array_equal(actual, pixels)
    elif size in (9, 32):assert np.any(actual != pixels)
    path = tmp_path/"flat.ijx";action(editor, "save_document", path=str(path))
    reopened = _desktop.Editor(str(tmp_path/"open.json"));reopened.open_file(str(path))
    np.testing.assert_array_equal(output(reopened).dataset.numpy("YXS"), actual)
    assert owner.snapshot().overlays == [row]


def test_repeated_text_and_point_digits_share_captured_resources(tmp_path):
    editor, owner, reads, pixels, row = fixture(tmp_path, text="A12 A12\n12", size=9)
    point = ij.with_point_style(ij.Selection("multipoint", [(24, 16), (40, 30)]), ij.PointStyle())
    tx = owner.begin_edit();tx.set_overlays([row, selection_record(point), row]);owner.commit(tx)
    # All three owners fit with actual file/coverage staging, not a fixed64MiB text reservation.
    editor.update_settings(json.dumps({"processing_memory_mb": 8}), False)
    n = reads.counts().plane_reads;before = owner.snapshot();action(editor, "flatten")
    assert reads.counts().plane_reads == n+1 and owner.snapshot().shares_data(before)
    assert np.any(output(editor).dataset.numpy("YXS") != pixels)


def test_font_error_can_recover_on_next_request(tmp_path):
    editor, owner, reads, pixels, row = fixture(tmp_path, font_path=str(tmp_path/"missing.ttf"))
    n = reads.counts().plane_reads
    with pytest.raises((ValueError, RuntimeError)):action(editor, "flatten")
    row.pop("font_path");tx = owner.begin_edit();tx.set_overlays([row]);owner.commit(tx)
    before = owner.snapshot();action(editor, "flatten")
    assert reads.counts().plane_reads == n+1 and owner.snapshot().shares_data(before)
    assert np.any(output(editor).dataset.numpy("YXS") != pixels)


@pytest.mark.parametrize("host", ["cpp", "python"])
def test_actual_mixed_text_point_flatten(host):
    path = os.environ.get("IMAGEJXX_POINT_ANNOTATIONS_"+host.upper()+"_REPORT")
    if not path:pytest.skip("Requires real mixed text/point Flatten input capture")
    report = json.loads(Path(path).read_bytes());state = report["text_flatten"]
    assert state["shape"] == [48, 64, 3] and state["overlay_kinds"] == ["text", "multipoint", "text"]
    assert state["source_unchanged"] and state["documents_added"] == 1
    np.testing.assert_array_equal(state["pixels"], state["expected_legacy_pixels"])
    assert len(report["frames"]) < 180
