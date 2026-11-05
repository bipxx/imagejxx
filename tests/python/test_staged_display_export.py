"""Final display-file writer compatibility and prepared output publication."""
import json
import zlib
import imagejxx as ij
import numpy as np
import pytest
from imagejxx import _desktop


def action(editor, name, **values):editor.action(name, json.dumps(values))


def fixture(tmp_path):
    pixels = np.arange(24*18*3, dtype=np.uint8).reshape(18, 24, 3)
    reads = ij.DatasetReadObserver()
    source = ij.Document(ij.Dataset(pixels, axes="YXS").observe_reads(reads))
    editor = _desktop.Editor(str(tmp_path/"preferences.json"));editor.add_native_document(source)
    return editor, source, reads, pixels


@pytest.mark.parametrize("suffix", ["png", "jpg", "jpeg", "bmp", "tga", "pgm", "ppm", "pnm", "npy", "tif", "tiff"])
def test_display_export_replaces_and_reopens_every_format(suffix, tmp_path):
    editor, source, reads, pixels = fixture(tmp_path);before = source.snapshot();count = reads.counts().plane_reads
    path = tmp_path/("display."+suffix);path.write_bytes(b"previous destination")
    try:action(editor, "export_display", path=str(path))
    except RuntimeError as error:
        if suffix in ("tif", "tiff") and "optional libtiff" in str(error):pytest.skip("Optional TIFF codec unavailable")
        raise
    assert reads.counts().plane_reads == count+1 and source.snapshot().shares_data(before)
    assert len(json.loads(editor.document_infos())) == 1 and path.read_bytes() != b"previous destination"
    reopened = _desktop.Editor(str(tmp_path/("reopen-"+suffix+".json")))
    if suffix in ("tif", "tiff"):reopened.open_legacy_file(str(path))
    else:reopened.open_file(str(path))
    actual = reopened.native_document(json.loads(reopened.document_infos())[0]["id"]).snapshot().dataset.numpy("YXS")
    assert actual.shape == pixels.shape
    if suffix not in ("jpg", "jpeg"):np.testing.assert_array_equal(actual, pixels)
    assert not list(tmp_path.glob("*.tmp.*"))


@pytest.mark.parametrize("landscape", [False, True])
def test_prepared_pdf_keeps_rgb_samples_and_layout(landscape, tmp_path):
    editor, source, reads, pixels = fixture(tmp_path);before = source.snapshot();count = reads.counts().plane_reads
    path = tmp_path/"display.pdf";path.write_bytes(b"keep")
    action(editor, "export_pdf", path=str(path), dpi=96, landscape=landscape)
    encoded = path.read_bytes();assert encoded.startswith(b"%PDF-1.4") and encoded.endswith(b"%%EOF\n")
    start = encoded.index(b"stream\n")+7;end = encoded.index(b"\nendstream", start)
    np.testing.assert_array_equal(np.frombuffer(zlib.decompress(encoded[start:end]), dtype=np.uint8).reshape(pixels.shape), pixels)
    box = b"792.000000 612.000000" if landscape else b"612.000000 792.000000"
    assert b"/MediaBox [0 0 "+box+b"]" in encoded
    position = b"387.000000 299.250000" if landscape else b"297.000000 389.250000"
    assert b"18.000000 0 0 13.500000 "+position+b" cm" in encoded
    assert source.snapshot().shares_data(before) and reads.counts().plane_reads == count+1
    assert not list(tmp_path.glob("*.tmp.*"))


@pytest.mark.parametrize("dtype", [np.uint8, np.uint16])
@pytest.mark.parametrize("samples", [1, 3, 4])
def test_generic_png_save_keeps_original_dtype(dtype, samples, tmp_path):
    shape = (6, 9) if samples == 1 else (6, 9, samples)
    pixels = np.arange(np.prod(shape), dtype=dtype).reshape(shape)
    if dtype == np.uint16:pixels *= 257
    owner = ij.Document(ij.Dataset(pixels, axes="YX" if samples == 1 else "YXS"))
    editor = _desktop.Editor(str(tmp_path/"prefs.json"));editor.add_native_document(owner)
    before = owner.snapshot();path = tmp_path/"original.png"
    action(editor, "save_pixels", path=str(path))
    reopened = _desktop.Editor(str(tmp_path/"open.json"));reopened.open_file(str(path))
    actual = reopened.native_document(json.loads(reopened.document_infos())[0]["id"]).snapshot().dataset.numpy("YX" if samples == 1 else "YXS")
    assert actual.dtype == np.dtype(dtype)
    np.testing.assert_array_equal(actual, pixels)
    assert owner.snapshot().shares_data(before)
