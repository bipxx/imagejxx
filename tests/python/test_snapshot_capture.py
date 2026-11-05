"""Native snapshot admission and actual export capture ownership."""
import json
import numpy as np
import pytest
import imagejxx as ij
from imagejxx import _desktop


def document():
    reads = ij.DatasetReadObserver()
    source = ij.Document(ij.Dataset(np.zeros((18, 24, 3), np.uint8), axes="YXS").observe_reads(reads))
    return source, reads


@pytest.mark.parametrize("controller", [False, True])
def test_capture_api_budget_identity_and_stale(controller):
    source, reads = document()
    view = source.snapshot().display
    view.properties = {"opaque": "x"*8192, "list": ["a", "b"*4096], "tree": {"nested": [1, 2]}}
    source.set_display(view)
    app = ij.Application(); app.adopt(source)
    owner = app if controller else source
    plan = owner.prepare_snapshot_capture(1 << 20)
    assert isinstance(plan, ij.SnapshotCapturePlan) and plan.document_id == source.id
    assert 12288 < plan.bytes < 65536
    with pytest.raises(ValueError, match="memory limit"):
        owner.prepare_snapshot_capture(plan.bytes-1)
    exact = owner.prepare_snapshot_capture(plan.bytes)
    snapshot = owner.capture_snapshot(exact)
    assert snapshot.shares_data(source.snapshot()) and snapshot.display.properties == view.properties
    tx = source.begin_edit();tx.set_title("changed");source.commit(tx)
    with pytest.raises(ij.RevisionConflict):owner.capture_snapshot(exact)
    assert reads.counts().plane_reads == 0


def test_capture_membership_is_explicit_and_closed_owner_refuses():
    source, _ = document();other, _ = document()
    app = ij.Application();app.adopt(source)
    plan = app.prepare_snapshot_capture(1 << 20)
    app.adopt(other)
    assert app.capture_snapshot(plan).id == source.id
    app.close(source.id)
    with pytest.raises(ij.DocumentUnavailable):app.capture_snapshot(plan)
    assert source.capture_snapshot(plan).id == source.id
    with pytest.raises(ValueError):other.capture_snapshot(plan)


@pytest.mark.parametrize("name", ["flatten", "export_display", "export_pdf"])
@pytest.mark.parametrize("limit", ["processing_memory_mb", "render_memory_mb"])
def test_export_rejects_large_capture_without_io_and_recovers(name, limit, tmp_path):
    source, reads = document()
    editor = _desktop.Editor(str(tmp_path/"preferences.json"));editor.add_native_document(source)
    view = source.snapshot().display;view.properties = {"lut": "x"*(2 << 20)};source.set_display(view)
    editor.update_settings(json.dumps({limit: 1}), False)
    before = reads.counts().plane_reads;count = len(editor.jobs)
    destination = tmp_path/("out.pdf" if name == "export_pdf" else "out.png");destination.write_bytes(b"keep")
    with pytest.raises((ValueError, RuntimeError), match="memory|buffers"):
        editor.action(name, json.dumps({"path": str(destination), "asynchronous": True}))
    assert reads.counts().plane_reads == before and not editor.busy and len(editor.jobs) == count
    assert destination.read_bytes() == b"keep" and json.loads(editor.render_memory_info())["working_bytes"] == 0
    view.properties = {};source.set_display(view);editor.clear_error()
    editor.action(name, json.dumps({"path": str(destination)}))
    assert not editor.error and not editor.busy and reads.counts().plane_reads == before+1
    assert json.loads(editor.render_memory_info())["working_bytes"] == 0


@pytest.mark.parametrize("name", ["flatten", "export_display", "export_pdf"])
def test_ready_validation_bounds_enlarged_live_metadata(name, tmp_path):
    import time
    source, reads = document()
    editor = _desktop.Editor(str(tmp_path/"preferences.json"));editor.add_native_document(source)
    destination = tmp_path/("out.pdf" if name == "export_pdf" else "out.png");destination.write_bytes(b"keep")
    before = reads.counts().plane_reads
    editor.action(name, json.dumps({"path": str(destination), "asynchronous": True}))
    deadline = time.monotonic()+5
    while editor.job_info.state in (ij.JobState.QUEUED, ij.JobState.RUNNING):
        assert time.monotonic()<deadline
        time.sleep(.001)
    assert editor.job_info.state == ij.JobState.READY
    view = source.snapshot().display;view.properties = {"lut": "x"*(2 << 20)};source.set_display(view)
    editor.wait()
    assert editor.error and "memory limit" in editor.error
    assert destination.read_bytes() == b"keep" and len(json.loads(editor.document_infos())) == 1
    assert reads.counts().plane_reads == before+1 and json.loads(editor.render_memory_info())["working_bytes"] == 0


def test_desktop_keeps_opaque_display_property_refusal(tmp_path):
    source, reads = document()
    editor = _desktop.Editor(str(tmp_path/"preferences.json"));editor.add_native_document(source)
    original = source.snapshot();before = reads.counts().plane_reads
    view = original.display;view.properties = {"opaque": "x"*(2 << 20)}
    with pytest.raises(ValueError, match="Unsupported desktop display property"):
        source.set_display(view)
    assert source.snapshot().display.properties == original.display.properties
    assert source.snapshot().revisions.display == original.revisions.display
    assert reads.counts().plane_reads == before and not editor.busy


@pytest.mark.parametrize("name", ["export_display", "export_pdf"])
@pytest.mark.parametrize("path", [None, 7, "x"*131065], ids=["null", "number", "oversized"])
def test_export_path_refusal_preserves_job_and_source(name, path, tmp_path):
    source, reads = document()
    editor = _desktop.Editor(str(tmp_path/"preferences.json"));editor.add_native_document(source)
    before = reads.counts().plane_reads;count = len(editor.jobs)
    with pytest.raises(ValueError, match="bounded output path"):
        editor.action(name, json.dumps({"path": path, "asynchronous": True}))
    assert not editor.busy and len(editor.jobs) == count and reads.counts().plane_reads == before
    assert json.loads(editor.render_memory_info())["working_bytes"] == 0
