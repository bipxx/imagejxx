"""Async export job lifecycle and synchronous script compatibility."""
import json
import time
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop


def action(editor, name, **options):editor.action(name, json.dumps(options))


def fixture(tmp_path):
    pixels = np.arange(18*24*3, dtype=np.uint8).reshape(18, 24, 3)
    reads = ij.DatasetReadObserver()
    source = ij.Document(ij.Dataset(pixels, axes="YXS").observe_reads(reads))
    editor = _desktop.Editor(str(tmp_path/"preferences.json"));editor.add_native_document(source)
    return editor, source, reads


def ready(editor):
    deadline = time.monotonic()+5
    while editor.job_info.state in (ij.JobState.QUEUED, ij.JobState.RUNNING):
        assert time.monotonic() < deadline
        time.sleep(.001)
    assert editor.job_info.state == ij.JobState.READY


@pytest.mark.parametrize("name", ["flatten", "export_display", "export_pdf"])
@pytest.mark.parametrize("finish", ["publish", "cancel", "stale"])
def test_async_deferred_output_and_ready_lifecycle(name, finish, tmp_path):
    editor, source, reads = fixture(tmp_path);before = source.snapshot();n = reads.counts().plane_reads
    path = tmp_path/("display.pdf" if name == "export_pdf" else "display.png");path.write_bytes(b"keep")
    action(editor, name, path=str(path), asynchronous=True)
    assert editor.busy and path.read_bytes() == b"keep" and len(json.loads(editor.document_infos())) == 1
    ready(editor);assert reads.counts().plane_reads == n+1 and editor.job_info.peak_reserved_bytes > 0
    if finish == "cancel":
        assert editor.cancel_job(editor.job_info.id)
        assert not list(tmp_path.glob("*.tmp.*"))
    elif finish == "stale":
        tx = source.begin_edit();tx.select(ij.Selection("rectangle", [(1, 1), (4, 4)]));source.commit(tx)
    editor.wait();assert not editor.busy and not list(tmp_path.glob("*.tmp.*"))
    if finish == "publish":
        assert not editor.error and source.snapshot().shares_data(before)
        assert len(json.loads(editor.document_infos())) == (2 if name == "flatten" else 1)
        assert (path.read_bytes() != b"keep") == (name != "flatten")
    else:
        assert path.read_bytes() == b"keep" and len(json.loads(editor.document_infos())) == 1
        if finish == "stale":assert editor.error


def test_sync_python_propagates_worker_error_and_recovers(tmp_path):
    editor, source, reads = fixture(tmp_path)
    tx = source.begin_edit();tx.set_overlays([{"kind":"text", "points":[[1, 1]], "text":"bad", "font_size":0}]);source.commit(tx)
    n = reads.counts().plane_reads
    with pytest.raises(ValueError, match="Font size"):action(editor, "flatten")
    assert not editor.busy and reads.counts().plane_reads == n and len(json.loads(editor.document_infos())) == 1
    tx = source.begin_edit();tx.set_overlays([]);source.commit(tx);editor.clear_error()
    action(editor, "flatten")
    assert not editor.busy and len(json.loads(editor.document_infos())) == 2 and reads.counts().plane_reads == n+1


@pytest.mark.parametrize("suffix", ["jpg", "jpeg", "tga"])
def test_format_dimensions_refuse_before_provider_read(suffix, tmp_path):
    reads = ij.DatasetReadObserver()
    source = ij.Document(ij.Dataset(np.zeros((1, 65536, 3), np.uint8), axes="YXS").observe_reads(reads))
    editor = _desktop.Editor(str(tmp_path/"preferences.json"));editor.add_native_document(source);n = reads.counts().plane_reads
    path = tmp_path/("too-wide."+suffix)
    with pytest.raises(ValueError, match="65535"):action(editor, "export_display", path=str(path))
    assert reads.counts().plane_reads == n and not editor.busy and not path.exists()


def test_sync_python_propagates_file_failure(tmp_path):
    editor, source, reads = fixture(tmp_path);before = source.snapshot()
    with pytest.raises(RuntimeError):action(editor, "export_display", path=str(tmp_path/"missing"/"output.png"))
    assert not editor.busy and len(json.loads(editor.document_infos())) == 1 and source.snapshot().shares_data(before)
    assert not list(tmp_path.rglob("*.tmp.*"))


@pytest.mark.parametrize("axis", ["T", "C"])
@pytest.mark.parametrize("name", ["flatten", "export_display", "export_pdf"])
def test_unsupported_source_refuses_before_job_submission(axis, name, tmp_path):
    reads = ij.DatasetReadObserver()
    source = ij.Document(ij.Dataset(np.zeros((2, 4, 5), np.uint8), axes=axis+"YX").observe_reads(reads))
    editor = _desktop.Editor(str(tmp_path/"preferences.json"));editor.add_native_document(source)
    n = reads.counts().plane_reads;job_count = len(editor.jobs)
    with pytest.raises(ValueError, match="scientific desktop"):
        action(editor, name, path=str(tmp_path/"invalid.png"), asynchronous=True)
    assert not editor.busy and reads.counts().plane_reads == n and len(editor.jobs) == job_count
